// DesktopDuplication.cpp
#include "DesktopDuplication.h"
#include "util.h"

using Microsoft::WRL::ComPtr;

bool DesktopDuplication::init(int adapterIndex, int outputIndex) {
    adapterIndex_ = adapterIndex;
    outputIndex_ = outputIndex;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    ComPtr<IDXGIFactory1> f1; ThrowIfFailed(CreateDXGIFactory1(__uuidof(IDXGIFactory1), &f1), "CreateDXGIFactory1");
    ComPtr<IDXGIAdapter1> adp; ThrowIfFailed(f1->EnumAdapters1(adapterIndex, &adp), "EnumAdapters1");
    D3D_FEATURE_LEVEL fl;
    ThrowIfFailed(D3D11CreateDevice(adp.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION,
        &device_, &fl, &ctx_), "D3D11CreateDevice");

    return reinitDuplication();
}

bool DesktopDuplication::reinitDuplication() {
    dup_.Reset();
    staging_.Reset();

    ComPtr<IDXGIDevice> dxgiDevice;
    ThrowIfFailed(device_.As(&dxgiDevice), "As IDXGIDevice");
    ComPtr<IDXGIAdapter> adapter;
    ThrowIfFailed(dxgiDevice->GetAdapter(&adapter), "GetAdapter");

    ComPtr<IDXGIOutput> out;
    ThrowIfFailed(adapter->EnumOutputs(outputIndex_, &out), "EnumOutputs");
    ComPtr<IDXGIOutput1> out1; ThrowIfFailed(out.As(&out1), "As IDXGIOutput1");
    ThrowIfFailed(out1->DuplicateOutput(device_.Get(), &dup_), "DuplicateOutput");

    // Describe staging texture later after first frame (size known)
    return true;
}

void DesktopDuplication::reset() {
    try {
        reinitDuplication();
    } catch (...) {
        // Ignore errors during reset
    }
}

bool DesktopDuplication::acquire(uint8_t*& mapped, int& pitch) {
    if (!dup_) return false;

    DXGI_OUTDUPL_FRAME_INFO fi{};
    ComPtr<IDXGIResource> res;
    HRESULT hr = dup_->AcquireNextFrame(33, &fi, &res);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) return false;

    // Handle access lost - need to recreate duplication
    if (hr == DXGI_ERROR_ACCESS_LOST) {
        reset();
        return false;
    }

    ThrowIfFailed(hr, "AcquireNextFrame");
    ComPtr<ID3D11Texture2D> tex; ThrowIfFailed(res.As(&tex), "As Tex2D");

    D3D11_TEXTURE2D_DESC desc{};
    tex->GetDesc(&desc);
    if (!staging_ || desc.Width != fullDesc_.Width || desc.Height != fullDesc_.Height) {
        fullDesc_ = desc;
        fullDesc_.BindFlags = 0;
        fullDesc_.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        fullDesc_.Usage = D3D11_USAGE_STAGING;
        fullDesc_.MiscFlags = 0;
        fullDesc_.MipLevels = 1;
        fullDesc_.ArraySize = 1;
        ThrowIfFailed(device_->CreateTexture2D(&fullDesc_, nullptr, &staging_), "CreateTexture2D staging");
    }
    ctx_->CopyResource(staging_.Get(), tex.Get());

    D3D11_MAPPED_SUBRESOURCE ms{};
    ThrowIfFailed(ctx_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &ms), "Map staging");
    mapped = reinterpret_cast<uint8_t*>(ms.pData);
    pitch  = ms.RowPitch;
    return true;
}

void DesktopDuplication::release() {
    if (staging_) ctx_->Unmap(staging_.Get(), 0);
    if (dup_) dup_->ReleaseFrame();
}

bool DesktopDuplication::copyRegionToBGRX(const CaptureRect& r, std::vector<uint8_t>& out, bool vflip, bool hflip) {
    uint8_t* base; int pitch;
    if (!acquire(base, pitch)) return false;
    out.resize(r.w * r.h * 4);
    for (int y = 0; y < r.h; ++y) {
        const int sy = vflip ? (r.h - 1 - y) : y;
        const uint8_t* src = base + (r.y + sy) * pitch + (r.x * 4);
        if (!hflip) {
            memcpy(out.data() + y * r.w * 4, src, r.w * 4);
        } else {
            uint8_t* dst = out.data() + y * r.w * 4;
            for (int x = 0; x < r.w; ++x) {
                const uint8_t* s = src + (r.w - 1 - x) * 4;
                memcpy(dst + x * 4, s, 4);
            }
        }
    }
    release();
    return true;
}

int DesktopDuplication::getOutputIndexForRegion(const RECT& region) {
    // Get the center point of the region
    int centerX = (region.left + region.right) / 2;
    int centerY = (region.top + region.bottom) / 2;

    // Enumerate all monitors to find which one contains the center point
    HMONITOR hMonitor = MonitorFromPoint({centerX, centerY}, MONITOR_DEFAULTTONEAREST);

    // Get monitor info to find the device name
    MONITORINFOEXW monitorInfo = {};
    monitorInfo.cbSize = sizeof(MONITORINFOEXW);
    if (!GetMonitorInfoW(hMonitor, &monitorInfo)) {
        return 0; // Default to primary monitor
    }

    // Enumerate DXGI outputs to find matching monitor
    try {
        ComPtr<IDXGIFactory1> factory;
        ThrowIfFailed(CreateDXGIFactory1(__uuidof(IDXGIFactory1), &factory), "CreateDXGIFactory1");

        ComPtr<IDXGIAdapter1> adapter;
        for (UINT adapterIndex = 0; factory->EnumAdapters1(adapterIndex, &adapter) != DXGI_ERROR_NOT_FOUND; ++adapterIndex) {
            ComPtr<IDXGIOutput> output;
            for (UINT outputIndex = 0; adapter->EnumOutputs(outputIndex, &output) != DXGI_ERROR_NOT_FOUND; ++outputIndex) {
                DXGI_OUTPUT_DESC desc;
                if (SUCCEEDED(output->GetDesc(&desc))) {
                    // Check if this output's desktop coordinates match our monitor
                    if (desc.DesktopCoordinates.left == monitorInfo.rcMonitor.left &&
                        desc.DesktopCoordinates.top == monitorInfo.rcMonitor.top &&
                        desc.DesktopCoordinates.right == monitorInfo.rcMonitor.right &&
                        desc.DesktopCoordinates.bottom == monitorInfo.rcMonitor.bottom) {
                        return outputIndex;
                    }
                }
                output.Reset();
            }
            adapter.Reset();
        }
    } catch (...) {
        // If anything fails, default to primary monitor
    }

    return 0; // Default to primary monitor
}

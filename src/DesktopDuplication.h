// DesktopDuplication.h
#pragma once
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl.h>
#include <vector>
#include <functional>

struct CaptureRect { int x, y, w, h; };

class DesktopDuplication {
public:
    bool init(int adapterIndex = 0, int outputIndex = 0);
    bool acquire(uint8_t*& mapped, int& pitch); // BGRA32 staging pointer
    void release();
    bool copyRegionToBGRX(const CaptureRect& r, std::vector<uint8_t>& out, bool vflip, bool hflip);
    void reset(); // Reset duplication after access lost

    // Helper function to determine which output a region belongs to
    static int getOutputIndexForRegion(const RECT& region);

private:
    bool reinitDuplication();

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx_;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> dup_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_; // full desktop staging
    D3D11_TEXTURE2D_DESC fullDesc_{};
    int adapterIndex_ = 0;
    int outputIndex_ = 0;
};

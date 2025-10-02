#pragma once
#include <windows.h>
#include <wrl.h>
#include <string>
#include <stdexcept>
#include <algorithm>
#include <mfapi.h>

inline void ThrowIfFailed(HRESULT hr, const char* msg) {
    if (FAILED(hr)) throw std::runtime_error(std::string(msg) + " hr=0x" + std::to_string(hr));
}

inline void EnableDpiAware() {
    // Per-monitor v2 if available
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    using SetCtx = BOOL (WINAPI*)(HANDLE);
    if (auto fn = reinterpret_cast<SetCtx>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")))
        fn(reinterpret_cast<HANDLE>(-4)); // PER_MONITOR_AWARE_V2
}

inline LONGLONG HnsFromFps(int fps) {
    return 10'000'000LL / (fps > 0 ? fps : 30);
}

// Simple BGRX (D3D staged) -> NV12 (CPU) converter for small/medium resolutions.
inline void BGRXtoNV12(const uint8_t* bgrx, int w, int h, int stride,
                       uint8_t* yPlane, int yStride, uint8_t* uvPlane, int uvStride) {
    for (int y = 0; y < h; ++y) {
        const uint8_t* src = bgrx + y * stride;
        uint8_t* ydst = yPlane + y * yStride;
        for (int x = 0; x < w; ++x) {
            uint8_t B = src[4*x + 0], G = src[4*x + 1], R = src[4*x + 2];
            int Y = (  66*R + 129*G +  25*B + 128) >> 8; Y += 16;
            ydst[x] = (uint8_t)std::clamp(Y, 0, 255);
        }
    }
    for (int y = 0; y < h; y += 2) {
        const uint8_t* src0 = bgrx + y * stride;
        const uint8_t* src1 = bgrx + (y+1) * stride;
        uint8_t* uv = uvPlane + (y/2) * uvStride;
        for (int x = 0; x < w; x += 2) {
            auto sample = [&](const uint8_t* s, int x) {
                uint8_t B=s[4*x+0], G=s[4*x+1], R=s[4*x+2];
                int U = ((-38*R - 74*G + 112*B + 128) >> 8) + 128;
                int V = ((112*R - 94*G -  18*B + 128) >> 8) + 128;
                return std::pair<int,int>(U,V);
            };
            auto uv0 = sample(src0,x);
            auto uv1 = sample(src0,x+1);
            auto uv2 = sample(src1,x);
            auto uv3 = sample(src1,x+1);
            int U0 = uv0.first, V0 = uv0.second;
            int U1 = uv1.first, V1 = uv1.second;
            int U2 = uv2.first, V2 = uv2.second;
            int U3 = uv3.first, V3 = uv3.second;
            int U = (U0+U1+U2+U3)/4; int V=(V0+V1+V2+V3)/4;
            uv[x+0] = (uint8_t)std::clamp(U,0,255);
            uv[x+1] = (uint8_t)std::clamp(V,0,255);
        }
    }
}

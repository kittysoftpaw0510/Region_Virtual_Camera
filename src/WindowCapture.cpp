// WindowCapture.cpp
#include "WindowCapture.h"
#include <stdio.h>
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

extern bool g_verbose;

WindowCapture::WindowCapture()
    : hwnd_(nullptr)
    , hdcWindow_(nullptr)
    , hdcMem_(nullptr)
    , hbmMem_(nullptr)
    , width_(0)
    , height_(0)
    , initialized_(false)
{
}

WindowCapture::~WindowCapture() {
    shutdown();
}

bool WindowCapture::init(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) {
        fprintf(stderr, "[WindowCapture] Invalid window handle\n");
        return false;
    }
    
    hwnd_ = hwnd;
    
    // Get window dimensions
    RECT rc;
    if (!GetClientRect(hwnd_, &rc)) {
        fprintf(stderr, "[WindowCapture] Failed to get window rect\n");
        return false;
    }
    
    width_ = rc.right - rc.left;
    height_ = rc.bottom - rc.top;
    
    // Ensure even dimensions for NV12
    if (width_ % 2 != 0) width_ = (width_ + 1) & ~1;
    if (height_ % 2 != 0) height_ = (height_ + 1) & ~1;
    
    // Get window DC
    hdcWindow_ = GetDC(hwnd_);
    if (!hdcWindow_) {
        fprintf(stderr, "[WindowCapture] Failed to get window DC\n");
        return false;
    }
    
    // Create compatible DC
    hdcMem_ = CreateCompatibleDC(hdcWindow_);
    if (!hdcMem_) {
        fprintf(stderr, "[WindowCapture] Failed to create compatible DC\n");
        ReleaseDC(hwnd_, hdcWindow_);
        hdcWindow_ = nullptr;
        return false;
    }
    
    // Create bitmap
    hbmMem_ = CreateCompatibleBitmap(hdcWindow_, width_, height_);
    if (!hbmMem_) {
        fprintf(stderr, "[WindowCapture] Failed to create bitmap\n");
        DeleteDC(hdcMem_);
        ReleaseDC(hwnd_, hdcWindow_);
        hdcWindow_ = nullptr;
        hdcMem_ = nullptr;
        return false;
    }
    
    SelectObject(hdcMem_, hbmMem_);

    initialized_ = true;
    if (g_verbose) {
        printf("[WindowCapture] Initialized for window %dx%d\n", width_, height_);
    }

    return true;
}

bool WindowCapture::captureWindow(std::vector<uint8_t>& bgrx, int& width, int& height) {
    if (!initialized_) {
        return false;
    }

    // Check if window still exists
    if (!IsWindow(hwnd_)) {
        fprintf(stderr, "[WindowCapture] Window no longer exists\n");
        return false;
    }

    // Try PrintWindow with RENDERFULLCONTENT flag first (best for modern apps)
    BOOL result = PrintWindow(hwnd_, hdcMem_, PW_RENDERFULLCONTENT);
    if (!result) {
        // Try without flag
        result = PrintWindow(hwnd_, hdcMem_, 0);
        if (!result) {
            // Last resort: BitBlt
            if (!BitBlt(hdcMem_, 0, 0, width_, height_, hdcWindow_, 0, 0, SRCCOPY)) {
                DWORD err = GetLastError();
                fprintf(stderr, "[WindowCapture] All capture methods failed: %d\n", err);
                return false;
            }
        }
    }

    // Get bitmap bits
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width_;
    bmi.bmiHeader.biHeight = -height_;  // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    bgrx.resize(width_ * height_ * 4);

    int lines = GetDIBits(hdcMem_, hbmMem_, 0, height_, bgrx.data(), &bmi, DIB_RGB_COLORS);
    if (lines == 0) {
        fprintf(stderr, "[WindowCapture] GetDIBits failed\n");
        return false;
    }

    width = width_;
    height = height_;

    return true;
}

void WindowCapture::shutdown() {
    if (hbmMem_) {
        DeleteObject(hbmMem_);
        hbmMem_ = nullptr;
    }
    
    if (hdcMem_) {
        DeleteDC(hdcMem_);
        hdcMem_ = nullptr;
    }
    
    if (hdcWindow_) {
        ReleaseDC(hwnd_, hdcWindow_);
        hdcWindow_ = nullptr;
    }
    
    initialized_ = false;
}

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    auto* windows = reinterpret_cast<std::vector<std::pair<HWND, std::wstring>>*>(lParam);
    
    // Skip invisible windows
    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }
    
    // Get window title
    wchar_t title[256];
    int len = GetWindowTextW(hwnd, title, 256);
    if (len == 0) {
        return TRUE;
    }
    
    // Skip windows with empty titles
    std::wstring titleStr(title);
    if (titleStr.empty()) {
        return TRUE;
    }
    
    // Check if window has visible content
    RECT rc;
    if (GetClientRect(hwnd, &rc)) {
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;
        
        // Skip tiny windows
        if (width > 100 && height > 100) {
            windows->push_back({hwnd, titleStr});
        }
    }
    
    return TRUE;
}

std::vector<std::pair<HWND, std::wstring>> WindowCapture::listWindows() {
    std::vector<std::pair<HWND, std::wstring>> windows;
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&windows));
    return windows;
}


// WindowCapture.h - Capture specific window using GDI/BitBlt
#pragma once
#include <windows.h>
#include <vector>
#include <string>

class WindowCapture {
public:
    WindowCapture();
    ~WindowCapture();
    
    bool init(HWND hwnd);
    bool captureWindow(std::vector<uint8_t>& bgrx, int& width, int& height);
    void shutdown();
    
    static std::vector<std::pair<HWND, std::wstring>> listWindows();
    
private:
    HWND hwnd_;
    HDC hdcWindow_;
    HDC hdcMem_;
    HBITMAP hbmMem_;
    int width_;
    int height_;
    bool initialized_;
};


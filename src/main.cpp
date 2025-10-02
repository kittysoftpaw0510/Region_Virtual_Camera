#include <windows.h>
#include <windowsx.h>
#include <shellscalingapi.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <cstdio>
#include <atomic>
#include "util.h"
#include "DesktopDuplication.h"
#include "WindowCapture.h"
#include "VirtualCamera.h"

#pragma comment(lib, "Shcore.lib")
#pragma comment(lib, "Gdiplus.lib")

struct Args {
    int fps = 30;
    bool vflip = false;
    bool hflip = false;
    bool preset = false;
    bool windowMode = false;
    bool listWindows = false;
    bool verbose = false;
    bool showHelp = false;
    RECT r{};
    std::wstring windowTitle;
};

// Global verbose flag (exported for other modules)
bool g_verbose = false;

#define LOG_VERBOSE(...) do { if (g_verbose) { printf(__VA_ARGS__); fflush(stdout); } } while(0)

static void ParseArgs(int argc, wchar_t** argv, Args& a) {
    for (int i=1; i<argc; ++i) {
        std::wstring s = argv[i];
        if (s == L"--fps" && i+1 < argc) a.fps = _wtoi(argv[++i]);
        else if (s == L"--vflip") a.vflip = true;
        else if (s == L"--hflip") a.hflip = true;
        else if (s == L"--x" && i+1<argc) { a.r.left = _wtoi(argv[++i]); a.preset = true; }
        else if (s == L"--y" && i+1<argc) { a.r.top  = _wtoi(argv[++i]); a.preset = true; }
        else if (s == L"--width"  && i+1<argc) { a.r.right  = a.r.left + _wtoi(argv[++i]); a.preset = true; }
        else if (s == L"--height" && i+1<argc) { a.r.bottom = a.r.top  + _wtoi(argv[++i]); a.preset = true; }
        else if (s == L"--window" && i+1<argc) { a.windowTitle = argv[++i]; a.windowMode = true; }
        else if (s == L"--list-windows") { a.listWindows = true; }
        else if (s == L"--verbose" || s == L"-v") { a.verbose = true; }
        else if (s == L"--help" || s == L"-h") { a.showHelp = true; }
    }
}

static void ShowHelp() {
    printf("RegionCam - Screen Region & Window to Virtual Camera\n");
    printf("High-performance C++ application for Windows that captures screen regions or specific windows and streams to OBS Virtual Camera.\n\n");

    printf("USAGE:\n");
    printf("  RegionCam.exe [OPTIONS]\n\n");

    printf("MODES:\n");
    printf("  (default)                    Interactive region selection mode\n");
    printf("  --window \"Window Title\"      Window capture mode\n");
    printf("  --list-windows               List available windows for capture\n\n");

    printf("OPTIONS:\n");
    printf("  --fps <number>               Set capture frame rate (15/30/60, default: 30)\n");
    printf("  --x <number>                 Set region X coordinate (preset mode)\n");
    printf("  --y <number>                 Set region Y coordinate (preset mode)\n");
    printf("  --width <number>             Set region width (preset mode)\n");
    printf("  --height <number>            Set region height (preset mode)\n");
    printf("  --vflip                      Flip video vertically\n");
    printf("  --hflip                      Flip video horizontally\n");
    printf("  --verbose, -v                Enable debug logging\n");
    printf("  --help, -h                   Show this help message\n\n");

    printf("EXAMPLES:\n");
    printf("  RegionCam.exe                                    # Interactive region selection\n");
    printf("  RegionCam.exe --window \"Chrome\"                 # Capture Chrome window\n");
    printf("  RegionCam.exe --window \"Chrome\" --hflip --vflip # Capture Chrome window with flipping\n");
    printf("  RegionCam.exe --x 100 --y 100 --width 800 --height 600 --fps 60  # Preset region\n\n");

    printf("REQUIREMENTS:\n");
    printf("  - Windows 10/11\n");
    printf("  - OBS Studio (for virtual camera driver)\n\n");

    printf("The captured content will be available as 'OBS Virtual Camera' in video applications.\n");
}

static RECT VirtualDesktop() {
    RECT r;
    r.left   = GetSystemMetrics(SM_XVIRTUALSCREEN);
    r.top    = GetSystemMetrics(SM_YVIRTUALSCREEN);
    r.right  = r.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    r.bottom = r.top  + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return r;
}

static RECT DragSelectOverlay() {
    EnableDpiAware();
    Gdiplus::GdiplusStartupInput gi; ULONG_PTR tok; Gdiplus::GdiplusStartup(&tok, &gi, nullptr);

    RECT vd = VirtualDesktop();
    int W = vd.right - vd.left, H = vd.bottom - vd.top;

    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"RegionPicker";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(WS_EX_LAYERED|WS_EX_TOPMOST|WS_EX_TOOLWINDOW, wc.lpszClassName, L"",
        WS_POPUP, vd.left, vd.top, W, H, nullptr, nullptr, wc.hInstance, nullptr);

    ShowWindow(hwnd, SW_SHOW);
    SetCursor(LoadCursor(nullptr, IDC_CROSS));

    // Rubber-band using layered window updates
    RECT sel{0,0,0,0}; bool dragging=false; POINT s{}, c{};
    MSG msg{};

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, W, H);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    auto UpdateOverlay = [&](RECT selection) {
        // Draw semi-transparent overlay with selection rectangle
        HBRUSH dark = CreateSolidBrush(RGB(0,0,0));
        RECT full{0,0,W,H};
        FillRect(mem, &full, dark);
        DeleteObject(dark);

        if (dragging && (selection.right - selection.left > 0) && (selection.bottom - selection.top > 0)) {
            // Draw bright rectangle for selection
            HPEN pen = CreatePen(PS_SOLID, 3, RGB(0,255,0));
            HGDIOBJ oldPen = SelectObject(mem, pen);
            HBRUSH nullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
            HGDIOBJ oldBrush = SelectObject(mem, nullBrush);

            int x1 = selection.left - vd.left;
            int y1 = selection.top - vd.top;
            int x2 = selection.right - vd.left;
            int y2 = selection.bottom - vd.top;

            Rectangle(mem, x1, y1, x2, y2);

            SelectObject(mem, oldBrush);
            SelectObject(mem, oldPen);
            DeleteObject(pen);
        }

        BLENDFUNCTION bf{AC_SRC_OVER,0, (BYTE)(255*0.35), 0};
        POINT ptSrc{0,0}, ptDst{ vd.left, vd.top };
        SIZE sz{W,H};
        UpdateLayeredWindow(hwnd, screen, &ptDst, &sz, mem, &ptSrc, 0, &bf, ULW_ALPHA);
    };

    UpdateOverlay(sel);

    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_LBUTTONDOWN) {
            dragging = true;
            s.x = GET_X_LPARAM(msg.lParam) + vd.left;
            s.y = GET_Y_LPARAM(msg.lParam) + vd.top;
            sel = RECT{ s.x, s.y, s.x, s.y };
            UpdateOverlay(sel);
        } else if (msg.message == WM_MOUSEMOVE && dragging) {
            c.x = GET_X_LPARAM(msg.lParam) + vd.left;
            c.y = GET_Y_LPARAM(msg.lParam) + vd.top;
            sel.right  = c.x;
            sel.bottom = c.y;
            UpdateOverlay(sel);
        } else if (msg.message == WM_LBUTTONUP && dragging) {
            dragging = false;
            break;
        } else if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            sel = RECT{0,0,0,0};
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    SelectObject(mem, oldBmp);
    DeleteDC(mem);
    DeleteObject(bmp);
    ReleaseDC(nullptr, screen);
    DestroyWindow(hwnd);
    Gdiplus::GdiplusShutdown(tok);

    // normalize
    if (sel.left > sel.right) std::swap(sel.left, sel.right);
    if (sel.top  > sel.bottom) std::swap(sel.top, sel.bottom);
    return sel;
}

// Global flag for clean shutdown
static std::atomic<bool> g_running{true};

// Helper function to apply flip operations during BGRX to RGB conversion
static void convertBGRXtoRGBWithFlip(const uint8_t* src, uint8_t* dst, int width, int height, bool vflip, bool hflip) {
    for (int y = 0; y < height; y++) {
        const int sy = vflip ? (height - 1 - y) : y;
        const uint8_t* srcRow = src + sy * width * 4;
        uint8_t* dstRow = dst + y * width * 3;

        if (!hflip) {
            // Normal horizontal order
            for (int x = 0; x < width; x++) {
                const uint8_t* s = srcRow + x * 4;
                uint8_t* d = dstRow + x * 3;
                d[0] = s[2];  // R
                d[1] = s[1];  // G
                d[2] = s[0];  // B
            }
        } else {
            // Flipped horizontal order
            for (int x = 0; x < width; x++) {
                const uint8_t* s = srcRow + (width - 1 - x) * 4;
                uint8_t* d = dstRow + x * 3;
                d[0] = s[2];  // R
                d[1] = s[1];  // G
                d[2] = s[0];  // B
            }
        }
    }
}

static BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType) {
    if (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_BREAK_EVENT || dwCtrlType == CTRL_CLOSE_EVENT) {
        printf("\nShutting down gracefully...\n");
        g_running = false;
        return TRUE;
    }
    return FALSE;
}

int wmain(int argc, wchar_t** argv) try {
    Args args; ParseArgs(argc, argv, args);
    g_verbose = args.verbose;

    if (g_verbose) {
        printf("[Main] Starting RegionCam...\n");
        fflush(stdout);
    }

    // Show help mode
    if (args.showHelp) {
        ShowHelp();
        return 0;
    }

    // List windows mode
    if (args.listWindows) {
        printf("\n=== Available Windows ===\n");
        auto windows = WindowCapture::listWindows();
        for (size_t i = 0; i < windows.size(); i++) {
            wprintf(L"%zu. %s\n", i + 1, windows[i].second.c_str());
        }
        printf("\nUse: RegionCam.exe --window \"Window Title\"\n");
        return 0;
    }

    // Window capture mode
    if (args.windowMode) {
        LOG_VERBOSE("[Main] Window capture mode\n");

        // Find window by title
        auto windows = WindowCapture::listWindows();
        HWND targetWindow = nullptr;

        for (const auto& [hwnd, title] : windows) {
            if (title.find(args.windowTitle) != std::wstring::npos) {
                targetWindow = hwnd;
                if (g_verbose) wprintf(L"[Main] Found window: %s\n", title.c_str());
                break;
            }
        }

        if (!targetWindow) {
            wprintf(L"ERROR: Window not found: %s\n", args.windowTitle.c_str());
            printf("Use --list-windows to see available windows\n");
            return 1;
        }

        WindowCapture winCap;
        if (!winCap.init(targetWindow)) {
            fprintf(stderr, "ERROR: Failed to initialize window capture\n");
            return 1;
        }

        // Get window size
        RECT rc;
        GetClientRect(targetWindow, &rc);
        int W = rc.right - rc.left;
        int H = rc.bottom - rc.top;

        // Ensure even dimensions
        if (W % 2 != 0) W = (W + 1) & ~1;
        if (H % 2 != 0) H = (H + 1) & ~1;

        printf("Capturing window: %dx%d @ %d FPS\n", W, H, args.fps);

        // Set up Ctrl+C handler
        SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

        VirtualCamera vc;
        vc.registerDevice(L"RegionCam Virtual Camera");

        std::vector<uint8_t> bgrx;
        VCConfig cfg{ W, H, args.fps };

        int frameCount = 0;
        vc.start(cfg, [&](uint8_t* rgb, int width, int height, int stride)->bool {
            int capW, capH;
            if (!winCap.captureWindow(bgrx, capW, capH)) {
                return false;
            }

            // Convert BGRX to RGB24 with flip support
            convertBGRXtoRGBWithFlip(bgrx.data(), rgb, width, height, args.vflip, args.hflip);

            frameCount++;
            if (g_verbose && frameCount % 30 == 0) {
                LOG_VERBOSE("[Main] Captured frame %d (%dx%d)\n", frameCount, width, height);
            }

            return g_running;
        });

        printf("Streaming to OBS Virtual Camera. Press Ctrl+C to stop.\n");
        LOG_VERBOSE("[Main] Entering main loop...\n");

        while (g_running) {
            Sleep(100);
        }

        LOG_VERBOSE("[Main] Stopping virtual camera...\n");
        vc.stop();

        LOG_VERBOSE("[Main] Cleanup complete.\n");
        return 0;
    }

    // Region capture mode (original behavior)
    RECT r = args.preset ? args.r : DragSelectOverlay();
    if ((r.right - r.left) <= 1 || (r.bottom - r.top) <= 1) {
        printf("No region selected, exiting.\n");
        return 0;
    }

    LOG_VERBOSE("[Main] Initializing Desktop Duplication...\n");

    // Determine which output (monitor) the selected region belongs to
    int outputIndex = DesktopDuplication::getOutputIndexForRegion(r);
    LOG_VERBOSE("[Main] Selected region is on output %d\n", outputIndex);

    DesktopDuplication dup;
    dup.init(0, outputIndex);

    // Calculate region size and ensure even dimensions for NV12
    int W = r.right - r.left;
    int H = r.bottom - r.top;

    // NV12 requires even width and height for chroma subsampling
    if (W % 2 != 0) W = (W + 1) & ~1;  // Round up to even
    if (H % 2 != 0) H = (H + 1) & ~1;  // Round up to even

    printf("Capturing region: %dx%d @ %d FPS\n", W, H, args.fps);

    LOG_VERBOSE("[Main] Registering virtual camera...\n");

    VirtualCamera vc;
    vc.registerDevice(L"RegionCam Virtual Camera");

    std::vector<uint8_t> bgrx;
    VCConfig cfg{ W, H, args.fps };

    LOG_VERBOSE("[Main] Starting virtual camera thread...\n");

    int frameCount = 0;
    vc.start(cfg, [&](uint8_t* rgb, int width, int height, int stride)->bool {
        CaptureRect cr{ r.left, r.top, W, H };
        if (!dup.copyRegionToBGRX(cr, bgrx, args.vflip, args.hflip)) return false;

        // Verify we got the expected size
        if (bgrx.size() != (size_t)(W * H * 4)) {
            fprintf(stderr, "[Main] ERROR: Captured size mismatch! Expected %d bytes, got %zu\n",
                    W * H * 4, bgrx.size());
            return false;
        }

        // Convert BGRX to RGB24 (flip operations already applied in copyRegionToBGRX)
        convertBGRXtoRGBWithFlip(bgrx.data(), rgb, width, height, false, false);

        // Debug: print every 30 frames (once per second at 30fps)
        frameCount++;
        if (g_verbose && frameCount % 30 == 0) {
            LOG_VERBOSE("[Main] Captured frame %d (%dx%d)\n", frameCount, width, height);
        }

        return true;
    });

    // Keep running until Ctrl+C
    printf("Streaming to OBS Virtual Camera. Press Ctrl+C to stop.\n");

    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    LOG_VERBOSE("[Main] Entering main loop...\n");

    while (g_running) {
        Sleep(100);
    }

    // Clean shutdown
    LOG_VERBOSE("[Main] Stopping virtual camera...\n");
    vc.stop();
    LOG_VERBOSE("[Main] Cleanup complete.\n");

    return 0;
}
catch (const std::exception& e) {
    fprintf(stderr, "Fatal error: %s\n", e.what());
    MessageBoxA(nullptr, e.what(), "RegionCam error", MB_ICONERROR|MB_OK);
    return 1;
}

// OBSVirtualCam.cpp
#include "OBSVirtualCam.h"
#include <stdio.h>
#include <string.h>

extern bool g_verbose;

OBSVirtualCam::OBSVirtualCam()
    : vq_(nullptr)
    , width_(0)
    , height_(0)
    , fps_(0)
    , active_(false)
    , frameCounter_(0)
    , haveClockFreq_(false)
{
}

OBSVirtualCam::~OBSVirtualCam() {
    shutdown();
}

bool OBSVirtualCam::initialize(int width, int height, int fps) {
    // Check if OBS Virtual Camera is installed
    HKEY key = nullptr;
    LPCWSTR guid = L"CLSID\\{A3FCE0F5-3493-419F-958A-ABA1250EC20B}";
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, guid, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        fprintf(stderr, "ERROR: OBS Virtual Camera not found! Please install OBS Studio.\n");
        return false;
    }
    RegCloseKey(key);

    width_ = width;
    height_ = height;
    fps_ = fps;

    if (g_verbose) {
        printf("[OBSVirtualCam] Initializing %dx%d @ %d FPS\n", width, height, fps);
        fflush(stdout);
    }

    // Allocate NV12 buffer
    nv12Buffer_.resize(width * height * 3 / 2);

    // Create video queue (interval in 100-nanosecond units)
    uint64_t interval = (uint64_t)(10000000.0 / fps);
    vq_ = video_queue_create(width, height, interval);

    if (!vq_) {
        fprintf(stderr, "ERROR: Failed to create video queue\n");
        fprintf(stderr, "Make sure no other application is using OBS Virtual Camera\n");
        return false;
    }

    active_ = true;
    if (g_verbose) {
        printf("[OBSVirtualCam] Successfully initialized!\n");
        printf("[OBSVirtualCam] OBS Virtual Camera is now available in video apps!\n");
        fflush(stdout);
    }

    return true;
}

uint64_t OBSVirtualCam::getTimestampNs() {
    if (!haveClockFreq_) {
        QueryPerformanceFrequency(&clockFreq_);
        haveClockFreq_ = true;
    }

    LARGE_INTEGER currentTime;
    QueryPerformanceCounter(&currentTime);
    double timeVal = (double)currentTime.QuadPart;
    timeVal *= 1000000000.0;
    timeVal /= (double)clockFreq_.QuadPart;

    return static_cast<uint64_t>(timeVal);
}

void OBSVirtualCam::rgbToNV12(const uint8_t* rgb, uint8_t* y, uint8_t* uv, int width, int height) {
    // RGB to NV12 conversion with proper 2x2 box filter for chroma
    // Uses BT.601 coefficients (same as libyuv default for RGB)

    // Y plane - full resolution
    for (int j = 0; j < height; j++) {
        const uint8_t* src = rgb + j * width * 3;
        uint8_t* dst = y + j * width;

        for (int i = 0; i < width; i++) {
            int r = src[0];
            int g = src[1];
            int b = src[2];

            // BT.601 full range: Y = 0.299*R + 0.587*G + 0.114*B
            // Exact coefficients from libyuv: 25, 129, 66
            dst[0] = (uint8_t)((25 * b + 129 * g + 66 * r + 128) >> 8);

            src += 3;
            dst += 1;
        }
    }

    // UV plane - subsampled 2x2 with proper box filter, interleaved
    for (int j = 0; j < height / 2; j++) {
        uint8_t* dst_uv = uv + j * width;

        for (int i = 0; i < width / 2; i++) {
            // Average 2x2 block for better chroma quality
            int r_sum = 0, g_sum = 0, b_sum = 0;

            for (int dy = 0; dy < 2; dy++) {
                for (int dx = 0; dx < 2; dx++) {
                    int srcY = j * 2 + dy;
                    int srcX = i * 2 + dx;

                    if (srcY < height && srcX < width) {
                        int idx = (srcY * width + srcX) * 3;
                        r_sum += rgb[idx + 0];
                        g_sum += rgb[idx + 1];
                        b_sum += rgb[idx + 2];
                    }
                }
            }

            // Average the 2x2 block
            int r = (r_sum + 2) >> 2;
            int g = (g_sum + 2) >> 2;
            int b = (b_sum + 2) >> 2;

            // BT.601 full range:
            // U = -0.169*R - 0.331*G + 0.500*B + 128
            // V =  0.500*R - 0.419*G - 0.081*B + 128
            // Exact coefficients from libyuv: 112, 74, 38 for U and 112, 94, 18 for V
            int u = ((112 * b - 74 * g - 38 * r + 128) >> 8) + 128;
            int v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;

            // Clamp
            dst_uv[0] = (uint8_t)(u < 0 ? 0 : (u > 255 ? 255 : u));
            dst_uv[1] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));

            dst_uv += 2;
        }
    }
}

bool OBSVirtualCam::sendFrame(const uint8_t* rgbData, int width, int height) {
    if (!active_ || !vq_) {
        return false;
    }

    if (width != width_ || height != height_) {
        fprintf(stderr, "[OBSVirtualCam] Frame size mismatch: %dx%d != %dx%d\n",
                width, height, width_, height_);
        return false;
    }

    // Convert RGB to NV12
    uint8_t* y = nv12Buffer_.data();
    uint8_t* uv = nv12Buffer_.data() + width * height;

    rgbToNV12(rgbData, y, uv, width, height);

    // Prepare data for queue
    // For NV12: Y plane has width stride, UV plane has width stride (interleaved U and V)
    uint8_t* data[2] = { y, uv };
    uint32_t linesize[2] = { (uint32_t)width, (uint32_t)width };

    uint64_t timestamp = getTimestampNs();

    // Write to OBS Virtual Camera queue
    video_queue_write(vq_, data, linesize, timestamp);

    frameCounter_++;
    return true;
}

void OBSVirtualCam::shutdown() {
    if (!active_) return;

    if (g_verbose) {
        printf("[OBSVirtualCam] Shutting down...\n");
        fflush(stdout);
    }

    active_ = false;

    if (vq_) {
        video_queue_close(vq_);
        vq_ = nullptr;
    }

    if (g_verbose) {
        printf("[OBSVirtualCam] Shutdown complete\n");
        fflush(stdout);
    }
}


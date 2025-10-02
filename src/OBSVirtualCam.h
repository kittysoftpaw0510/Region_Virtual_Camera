// OBSVirtualCam.h - Direct interface to OBS Virtual Camera
#pragma once
#include <windows.h>
#include <string>
#include <vector>

extern "C" {
#include "obs_queue/shared-memory-queue.h"
}

class OBSVirtualCam {
public:
    OBSVirtualCam();
    ~OBSVirtualCam();

    bool initialize(int width, int height, int fps);
    bool sendFrame(const uint8_t* rgbData, int width, int height);
    void shutdown();

    bool isActive() const { return active_; }

private:
    uint64_t getTimestampNs();
    void rgbToNV12(const uint8_t* rgb, uint8_t* y, uint8_t* uv, int width, int height);

    video_queue_t* vq_;
    int width_;
    int height_;
    int fps_;
    bool active_;
    uint64_t frameCounter_;
    std::vector<uint8_t> nv12Buffer_;

    bool haveClockFreq_;
    LARGE_INTEGER clockFreq_;
};


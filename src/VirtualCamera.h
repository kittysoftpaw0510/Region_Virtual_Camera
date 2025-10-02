// VirtualCamera.h
#pragma once
#include <functional>
#include <atomic>
#include <thread>
#include "OBSVirtualCam.h"

struct VCConfig {
    int width = 1280;
    int height = 720;
    int fps = 30;
};

class VirtualCamera {
public:
    // frameProvider(out_rgb, width, height, stride) must fill RGB24 frame data
    using FrameProvider = std::function<bool(uint8_t* rgb, int width, int height, int stride)>;

    VirtualCamera();
    ~VirtualCamera();

    void registerDevice(const wchar_t* friendlyName);
    void start(const VCConfig& cfg, FrameProvider provider);
    void stop();

private:
    void run();

    VCConfig cfg_;
    FrameProvider provider_;
    std::atomic<bool> running_{false};
    std::thread th_;
    OBSVirtualCam obsCam_;
};

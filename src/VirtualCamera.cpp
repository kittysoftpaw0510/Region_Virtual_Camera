// VirtualCamera.cpp
#include "VirtualCamera.h"
#include "util.h"
#include <chrono>
#include <stdio.h>

extern bool g_verbose;

VirtualCamera::VirtualCamera() {}

VirtualCamera::~VirtualCamera() { 
    stop(); 
}

void VirtualCamera::registerDevice(const wchar_t* friendlyName) {
    if (g_verbose) {
        printf("[VirtualCamera] Registering as: %S\n", friendlyName);
        printf("[VirtualCamera] Will use OBS Virtual Camera\n");
        fflush(stdout);
    }
}

void VirtualCamera::start(const VCConfig& cfg, FrameProvider provider) {
    cfg_ = cfg;
    provider_ = provider;
    
    // Initialize OBS Virtual Camera
    if (!obsCam_.initialize(cfg.width, cfg.height, cfg.fps)) {
        fprintf(stderr, "ERROR: Failed to initialize OBS Virtual Camera\n");
        return;
    }

    if (g_verbose) {
        printf("[VirtualCamera] OBS Virtual Camera initialized\n");
        printf("[VirtualCamera] You can now use 'OBS Virtual Camera' in any app!\n");
        fflush(stdout);
    }

    running_ = true;
    th_ = std::thread(&VirtualCamera::run, this);
}

void VirtualCamera::stop() {
    if (!running_) return;
    running_ = false;
    if (th_.joinable()) th_.join();
    
    obsCam_.shutdown();
}

void VirtualCamera::run() {
    try {
        if (g_verbose) {
            printf("[VirtualCamera] Thread started\n");
            fflush(stdout);
        }

        const int frameSize = cfg_.width * cfg_.height * 3; // RGB24
        std::vector<uint8_t> rgbFrame(frameSize);

        auto next = std::chrono::high_resolution_clock::now();
        const LONGLONG frameDur = HnsFromFps(cfg_.fps);
        int frameCount = 0;
        int consecutiveFailures = 0;

        while (running_) {
            try {
                // Get frame from provider
                if (!provider_(rgbFrame.data(), cfg_.width, cfg_.height, cfg_.width * 3)) {
                    consecutiveFailures++;
                    if (consecutiveFailures > 10) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                    continue;
                }
                consecutiveFailures = 0;

                // Send frame to OBS Virtual Camera
                if (!obsCam_.sendFrame(rgbFrame.data(), cfg_.width, cfg_.height)) {
                    fprintf(stderr, "[VirtualCamera] Failed to send frame\n");
                }

                frameCount++;
                if (g_verbose && frameCount % 30 == 0) {
                    printf("[VirtualCamera] Sent %d frames to OBS Virtual Camera\n", frameCount);
                    fflush(stdout);
                }

                next += std::chrono::nanoseconds(frameDur * 100);
                std::this_thread::sleep_until(next);
                
            } catch (const std::exception& e) {
                fprintf(stderr, "Frame processing error: %s\n", e.what());
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        if (g_verbose) {
            printf("[VirtualCamera] Thread exiting normally\n");
            fflush(stdout);
        }

    } catch (const std::exception& e) {
        fprintf(stderr, "FATAL: VirtualCamera thread crashed: %s\n", e.what());
        fflush(stderr);
        running_ = false;
    } catch (...) {
        fprintf(stderr, "FATAL: VirtualCamera thread crashed with unknown exception\n");
        fflush(stderr);
        running_ = false;
    }
}

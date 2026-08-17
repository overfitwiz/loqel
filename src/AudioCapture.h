#pragma once

#include <windows.h>

#include <thread>

#include "core/AudioCapture.h"

class WindowsAudioCapture final : public IAudioCapture {
public:
    ~WindowsAudioCapture() override;

    bool start(AudioQueue& queue) override;

    void stop() override;

private:
    void capture_loop(AudioQueue* queue);

    HANDLE stop_event_ = nullptr;

    std::thread thread_;
};

#pragma once

class AudioQueue;

class IAudioCapture {
public:
    virtual ~IAudioCapture() = default;

    virtual bool start(AudioQueue& queue) = 0;
    virtual void stop() = 0;
};

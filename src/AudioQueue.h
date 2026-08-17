#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

class AudioQueue {
public:
    void start(int sample_rate);

    bool wait_until_ready(
        int& sample_rate,
        std::string& error
    );

    bool push(std::vector<float> samples);

    bool pop(std::vector<float>& samples);

    void close();

    void fail(std::string error);

    std::string error() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;

    std::deque<std::vector<float>> chunks_;

    std::size_t queued_samples_ = 0;
    std::size_t max_queued_samples_ = 0;

    int sample_rate_ = 0;

    bool ready_ = false;
    bool closed_ = false;

    std::string error_;
};

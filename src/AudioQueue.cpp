#include "AudioQueue.h"

#include <utility>

void AudioQueue::start(int sample_rate) {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        sample_rate_ = sample_rate;

        // Allow at most 3 seconds of queued audio.
        max_queued_samples_ =
            static_cast<std::size_t>(sample_rate) * 3;

        ready_ = true;
    }

    changed_.notify_all();
}

bool AudioQueue::wait_until_ready(
    int& sample_rate,
    std::string& error
) {
    std::unique_lock<std::mutex> lock(mutex_);

    changed_.wait(
        lock,
        [this] {
            return ready_ || closed_;
        }
    );

    if (!ready_) {
        error = error_.empty()
            ? "Microphone stopped before initialization."
            : error_;

        return false;
    }

    sample_rate = sample_rate_;

    return true;
}

bool AudioQueue::push(std::vector<float> samples) {
    if (samples.empty()) {
        return true;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (closed_) {
            return false;
        }

        if (
            queued_samples_ + samples.size() >
            max_queued_samples_
        ) {
            error_ =
                "Audio consumer fell more than "
                "three seconds behind.";

            closed_ = true;

            changed_.notify_all();

            return false;
        }

        queued_samples_ += samples.size();

        chunks_.push_back(
            std::move(samples)
        );
    }

    changed_.notify_one();

    return true;
}

bool AudioQueue::pop(std::vector<float>& samples) {
    std::unique_lock<std::mutex> lock(mutex_);

    changed_.wait(
        lock,
        [this] {
            return !chunks_.empty() || closed_;
        }
    );

    if (chunks_.empty()) {
        return false;
    }

    samples = std::move(
        chunks_.front()
    );

    chunks_.pop_front();

    queued_samples_ -= samples.size();

    return true;
}

void AudioQueue::close() {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        closed_ = true;
    }

    changed_.notify_all();
}

void AudioQueue::fail(std::string error) {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (error_.empty()) {
            error_ = std::move(error);
        }

        closed_ = true;
    }

    changed_.notify_all();
}

std::string AudioQueue::error() const {
    std::lock_guard<std::mutex> lock(mutex_);

    return error_;
}

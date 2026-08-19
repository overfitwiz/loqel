#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

class LlmPostprocessor {
public:
    LlmPostprocessor();
    ~LlmPostprocessor();

    LlmPostprocessor(const LlmPostprocessor&) = delete;
    LlmPostprocessor& operator=(const LlmPostprocessor&) = delete;

    bool initialize(
        const std::filesystem::path& model_path,
        bool enabled,
        std::string& error
    );

    bool set_enabled(bool enabled, std::string& error);
    bool process(
        const std::string& text,
        std::string& output,
        std::string& error
    );

    void shutdown();
    bool enabled() const;
    bool loaded() const;

private:
    struct State;

    bool load_locked(std::string& error);
    void unload_locked();

    mutable std::mutex mutex_;
    std::filesystem::path model_path_;
    std::unique_ptr<State> state_;
    bool enabled_ = false;
    bool backend_initialized_ = false;
};

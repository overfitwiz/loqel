#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

#include "ApplicationPlatform.h"
#include "ApplicationSettings.h"
#include "AudioCapture.h"
#include "AsrEngine.h"
#include "AudioQueue.h"
#include "MarkdownFormatter.h"
#include "LlmPostprocessor.h"

class ApplicationCore {
public:
    ApplicationCore(
        IApplicationPlatform& platform,
        IAudioCapture& audio_capture
    );

    ~ApplicationCore();

    bool initialize(
        const std::filesystem::path& model_path,
        const std::filesystem::path& llm_model_path,
        std::string& error
    );

    void set_settings(
        const MarkdownCommands& commands,
        const CustomDictionarySettings& dictionary,
        const RecognitionSettings& recognition,
        const CleanupSettings& cleanup,
        const LlmSettings& llm,
        const DebugSettings& debug
    );

    void start_session(OutputMode output_mode);
    void stop_session();
    void shutdown();

    bool session_active() const;
    bool finalizing() const;

private:
    struct SessionResult {
        std::string text;
        std::string error;
        std::string debug_error;
    };

    void consume_audio();
    void handle_session_done(SessionResult result);
    void post_session_done(SessionResult result);

    IApplicationPlatform& platform_;
    IAudioCapture& audio_capture_;
    AsrEngine asr_;
    LlmPostprocessor llm_;

    MarkdownCommands markdown_commands_ = MarkdownCommands::defaults();
    MarkdownCommands active_markdown_commands_ = MarkdownCommands::defaults();
    CustomDictionarySettings custom_dictionary_;
    CustomDictionarySettings active_custom_dictionary_;
    RecognitionSettings recognition_settings_;
    RecognitionSettings active_recognition_settings_;
    CleanupSettings cleanup_settings_;
    CleanupSettings active_cleanup_settings_;
    LlmSettings llm_settings_;
    DebugSettings debug_settings_;
    DebugSettings active_debug_settings_;
    OutputMode active_output_mode_ = OutputMode::Formatted;

    std::unique_ptr<AudioQueue> audio_queue_;
    std::thread consumer_thread_;

    ActiveTarget target_ = 0;
    bool session_active_ = false;
    bool finalizing_ = false;
    std::atomic<bool> shutting_down_{false};
};

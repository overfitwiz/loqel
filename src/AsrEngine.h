#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <nemo_speech/asr.h>

struct AsrResult {
    std::string text;
    bool is_final = false;
};

class AsrEngine {
public:
    ~AsrEngine();

    bool load(
        const std::filesystem::path& model_path,
        int rnnt_right_context,
        std::string& error
    );

    bool reconfigure_streaming(
        int rnnt_right_context,
        std::string& error
    );

    bool start_stream(
        const std::vector<std::string>& speech_contexts,
        float speech_context_boost,
        std::string& error
    );

    bool push(
        const std::vector<float>& samples,
        int sample_rate,
        std::vector<AsrResult>& results,
        std::string& error
    );

    bool finish(
        std::vector<AsrResult>& results,
        std::string& error
    );

    bool recognize(
        const std::vector<float>& samples,
        int sample_rate,
        const std::vector<std::string>& speech_contexts,
        float speech_context_boost,
        std::string& transcript,
        std::string& error
    );

    void close_stream();

    bool loaded() const;
    int rnnt_right_context() const;

private:
    void unload();

    bool drain_results(
        std::vector<AsrResult>& results,
        std::string& error
    );

    static std::string make_error(
        const char* operation
    );

    nemo_speech_asr_recognizer* recognizer_ = nullptr;
    nemo_speech_asr_stream* stream_ = nullptr;

    std::string model_path_;
    std::filesystem::path model_file_;
    int rnnt_right_context_ = 6;
};

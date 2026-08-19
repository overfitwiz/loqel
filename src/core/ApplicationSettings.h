#pragma once

#include <string>
#include <vector>

enum class RecognitionMode {
    Streaming,
    RecordThenTranscribe
};

enum class LatencyPreset {
    Low,
    Balanced,
    HighestAccuracy
};

struct RecognitionSettings {
    RecognitionMode mode = RecognitionMode::Streaming;
    LatencyPreset latency = LatencyPreset::Balanced;
};

inline int rnnt_right_context(LatencyPreset preset) {
    switch (preset) {
        case LatencyPreset::Low:
            return 1;
        case LatencyPreset::Balanced:
            return 6;
        case LatencyPreset::HighestAccuracy:
            return -1;
    }

    return 6;
}

struct CustomDictionarySettings {
    std::vector<std::string> phrases;
    float boost = 2.0f;
};

struct CleanupSettings {
    std::vector<std::string> remove_words = {"Um", "uhm", "eh"};
};

struct LlmSettings {
    bool enabled = true;
};

struct HotkeySettings {
    int formatted_function_key = 8;
    int plain_function_key = 9;
};

enum class OutputMode {
    Formatted,
    Plain
};

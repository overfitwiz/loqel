#include "core/ApplicationSettings.h"

#include <iostream>

int main() {
    int failures = 0;

    const auto expect = [&](const char* name, int actual, int expected) {
        if (actual != expected) {
            std::cerr
                << "FAILED: " << name
                << " expected " << expected
                << ", got " << actual << '\n';
            ++failures;
        }
    };

    expect(
        "low latency",
        rnnt_right_context(LatencyPreset::Low),
        1
    );
    expect(
        "balanced latency",
        rnnt_right_context(LatencyPreset::Balanced),
        6
    );
    expect(
        "model latency",
        rnnt_right_context(LatencyPreset::HighestAccuracy),
        -1
    );

    const RecognitionSettings defaults;
    if (
        defaults.mode != RecognitionMode::Streaming ||
        defaults.latency != LatencyPreset::Balanced
    ) {
        std::cerr << "FAILED: recognition defaults\n";
        ++failures;
    }

    const LlmSettings llm_defaults;
    if (!llm_defaults.enabled) {
        std::cerr << "FAILED: LLM correction should default to enabled\n";
        ++failures;
    }

    const DebugSettings debug_defaults;
    if (debug_defaults.enabled) {
        std::cerr << "FAILED: debug mode should default to disabled\n";
        ++failures;
    }

    return failures == 0 ? 0 : 1;
}

#include "LlmCorrectionPolicy.h"
#include "LlmPostprocessor.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

std::string correct_and_check(
    LlmPostprocessor& processor,
    const std::string& original,
    const char* message
) {
    std::string corrected;
    std::string error;
    const bool ok = processor.process(original, corrected, error);

    if (!ok) {
        std::cerr << "FAILED: " << message << ": " << error << '\n';
        ++failures;
        return {};
    }

    std::cout << corrected << '\n';
    expect(
        llm_correction_length_is_safe(original, corrected),
        message
    );
    return corrected;
}

}

int main() {
    const std::filesystem::path model =
        "models/LFM2.5-350M-Q8_0.gguf";
    if (!std::filesystem::is_regular_file(model)) {
        std::cout << "SKIPPED: local LLM model is not installed\n";
        return 0;
    }

    LlmPostprocessor processor;
    std::string error;
    if (!processor.initialize(model, true, error)) {
        std::cerr << "FAILED: could not initialize LLM: " << error << '\n';
        return 1;
    }

    correct_and_check(
        processor,
        "Open github dot com. Then leave this sentence unchanged.",
        "a clear spoken-form result must remain safe"
    );

    correct_and_check(
        processor,
        "It works with both live and record first modes and persists in "
        "settings dot Ne. The BUP mode is disabled by default.",
        "the reported transcript must not be destructively shortened"
    );
    correct_and_check(
        processor,
        "Keep this first line exactly.\nKeep this second line too.",
        "a multiline transcript must retain every line"
    );

    processor.shutdown();
    return failures == 0 ? 0 : 1;
}

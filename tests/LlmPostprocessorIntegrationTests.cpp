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
    const std::string& expected,
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
    expect(corrected.find(expected) != std::string::npos, message);
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
        "Open github dot com slash pricing. Then leave this sentence unchanged.",
        "github.com",
        "a URL domain must be normalized"
    );

    correct_and_check(
        processor,
        "Email jane dot doe at example dot com.",
        "jane.doe@example.com",
        "an email address must be normalized"
    );
    correct_and_check(
        processor,
        "The total is twenty five dollars and fifty cents.",
        "$25.50",
        "a currency amount must be normalized"
    );
    processor.shutdown();
    return failures == 0 ? 0 : 1;
}

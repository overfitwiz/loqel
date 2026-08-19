#include "LlmCorrectionPolicy.h"

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

}

int main() {
    const std::string reported_original =
        "It works with both live and record first modes and persists in "
        "settings dot Ne. The BUP mode is disabled by default.";

    expect(
        !llm_correction_length_is_safe(
            reported_original,
            "the BUP mode is preserved in settings."
        ),
        "the reported destructive shortening must be rejected"
    );
    expect(
        llm_correction_length_is_safe(
            reported_original,
            "It works with both live and record-first modes and persists in "
            "settings.ini. The BUP mode is disabled by default."
        ),
        "a length-preserving correction should be accepted"
    );
    expect(
        llm_correction_length_is_safe(
            "it costs twenty five dollars",
            "it costs $25"
        ),
        "short spoken-form normalization should be accepted"
    );
    expect(
        !llm_correction_length_is_safe(
            "Keep this first line exactly.\nKeep this second line too.",
            "Keep this first line exactly."
        ),
        "removing a line must be rejected"
    );
    expect(
        !llm_correction_length_is_safe(
            "This ordinary sentence contains enough words for a safe check.",
            "This response invents a great deal of unrelated explanatory "
            "material that was never spoken and therefore must not be inserted."
        ),
        "substantial invented expansion must be rejected"
    );

    return failures == 0 ? 0 : 1;
}

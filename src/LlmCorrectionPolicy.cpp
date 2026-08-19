#include "LlmCorrectionPolicy.h"

#include <algorithm>
#include <cctype>
#include <cstddef>

namespace {

std::size_t word_count(const std::string& text) {
    std::size_t count = 0;
    bool in_word = false;

    for (unsigned char character : text) {
        const bool is_word =
            std::isalnum(character) != 0 || character >= 0x80;
        if (is_word && !in_word) {
            ++count;
        }
        in_word = is_word;
    }

    return count;
}

std::size_t newline_count(const std::string& text) {
    return static_cast<std::size_t>(
        std::count(text.begin(), text.end(), '\n')
    );
}

bool contains_compact_written_form(const std::string& text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char character = text[i];
        if (
            std::isdigit(character) != 0 ||
            character == '$' || character == '%' || character == '@' ||
            character == '/' || character == '\\'
        ) {
            return true;
        }

        // A period or colon is compacting only inside a written form. Do not
        // let ordinary sentence punctuation weaken the short-text guard.
        if (
            (character == '.' || character == ':') &&
            i > 0 && i + 1 < text.size() &&
            std::isalnum(static_cast<unsigned char>(text[i - 1])) != 0 &&
            std::isalnum(static_cast<unsigned char>(text[i + 1])) != 0
        ) {
            return true;
        }
    }

    return false;
}

}

bool llm_correction_length_is_safe(
    const std::string& original,
    const std::string& corrected
) {
    if (corrected.empty()) {
        return false;
    }

    // Losing a line is always suspicious. Adding or moving line breaks is left
    // to the prompt because punctuation correction can legitimately do that.
    if (newline_count(corrected) < newline_count(original)) {
        return false;
    }

    const std::size_t original_words = word_count(original);
    const std::size_t corrected_words = word_count(corrected);

    if (original_words <= 2) {
        return true;
    }

    // Compact written forms can replace several spoken words. Otherwise allow
    // at most roughly one sixth of normal prose to disappear, with a small
    // absolute tolerance for short sentences.
    const std::size_t allowed_loss = original_words < 8
        ? (contains_compact_written_form(corrected) ? 3 : 1)
        : std::max<std::size_t>(2, original_words / 6);
    if (corrected_words + allowed_loss < original_words) {
        return false;
    }

    // Normalization should not invent a substantially longer response either.
    const std::size_t allowed_growth = std::max<std::size_t>(
        3,
        original_words / 3
    );
    return corrected_words <= original_words + allowed_growth;
}

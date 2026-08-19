#pragma once

#include <string>

// LLM correction is optional. A rejected correction must never replace the
// recognized transcript.
bool llm_correction_length_is_safe(
    const std::string& original,
    const std::string& corrected
);

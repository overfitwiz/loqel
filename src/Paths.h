#pragma once

#include <filesystem>

namespace Paths {

std::filesystem::path executable_directory();

std::filesystem::path resolve_model_path(
    int argc,
    wchar_t** argv
);

std::filesystem::path resolve_llm_model_path(
    int argc,
    wchar_t** argv
);

}

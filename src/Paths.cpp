#include "Paths.h"

#include <windows.h>

#include <string>

namespace {

constexpr const wchar_t* kModelFilenames[] = {
    L"nemotron-speech-streaming-en-0.6b.q8_0.gguf",
    L"nemotron-speech-streaming-en-0.6b.new.q8_0.gguf"
};


std::filesystem::path make_absolute(
    const std::filesystem::path& path
) {
    std::error_code error;

    const auto absolute =
        std::filesystem::absolute(
            path,
            error
        );

    if (error) {
        return path;
    }

    return absolute;
}


std::filesystem::path search_up(
    std::filesystem::path directory
) {
    for (
        int depth = 0;
        depth < 6 && !directory.empty();
        ++depth
    ) {
        for (const wchar_t* filename : kModelFilenames) {
            const auto candidate =
                directory /
                L"models" /
                filename;

            std::error_code error;

            if (
                std::filesystem::is_regular_file(
                    candidate,
                    error
                )
            ) {
                return candidate;
            }
        }

        const auto parent =
            directory.parent_path();

        if (parent == directory) {
            break;
        }

        directory = parent;
    }

    return {};
}

}


namespace Paths {

std::filesystem::path executable_directory() {
    std::wstring buffer(
        32768,
        L'\0'
    );

    const DWORD length =
        GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(
                buffer.size()
            )
        );

    if (
        length == 0 ||
        length >= buffer.size()
    ) {
        return {};
    }

    buffer.resize(length);

    return
        std::filesystem::path(buffer)
        .parent_path();
}


std::filesystem::path resolve_model_path(
    int argc,
    wchar_t** argv
) {
    // 1. Command line:
    //
    // loqel.exe --model model.gguf

    for (
        int i = 1;
        i < argc;
        ++i
    ) {
        const std::wstring argument =
            argv[i];

        if (
            argument == L"--model" &&
            i + 1 < argc
        ) {
            return make_absolute(
                argv[i + 1]
            );
        }
    }


    // 2. Positional argument:
    //
    // loqel.exe model.gguf

    for (
        int i = 1;
        i < argc;
        ++i
    ) {
        if (
            argv[i][0] != L'-'
        ) {
            return make_absolute(
                argv[i]
            );
        }
    }


    // 3. Environment variable:
    //
    // NEMO_SPEECH_MODEL

    std::wstring environment(
        32768,
        L'\0'
    );

    const DWORD environment_length =
        GetEnvironmentVariableW(
            L"NEMO_SPEECH_MODEL",
            environment.data(),
            static_cast<DWORD>(
                environment.size()
            )
        );

    if (
        environment_length > 0 &&
        environment_length <
            environment.size()
    ) {
        environment.resize(
            environment_length
        );

        return make_absolute(
            environment
        );
    }


    // 4. Search upward from executable.

    if (
        auto found =
            search_up(
                executable_directory()
            );

        !found.empty()
    ) {
        return found;
    }


    // 5. Search upward from current working directory.

    std::error_code error;

    const auto current =
        std::filesystem::current_path(
            error
        );

    if (!error) {
        return search_up(current);
    }


    return {};
}

}

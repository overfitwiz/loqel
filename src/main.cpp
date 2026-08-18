#include <windows.h>
#include <shellapi.h>

#include <filesystem>

#include "App.h"
#include "Paths.h"

namespace {

constexpr wchar_t kAppName[] =
    L"loqel";

}

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int
) {
    HANDLE single_instance =
        CreateMutexW(
            nullptr,
            TRUE,
            L"Local\\loqel.PushToTalk"
        );

    if (
        !single_instance ||
        GetLastError() ==
            ERROR_ALREADY_EXISTS
    ) {
        MessageBoxW(
            nullptr,
            L"loqel is already running.",
            kAppName,
            MB_OK |
                MB_ICONINFORMATION
        );

        if (single_instance) {
            CloseHandle(
                single_instance
            );
        }

        return 1;
    }


    int argc = 0;

    wchar_t** argv =
        CommandLineToArgvW(
            GetCommandLineW(),
            &argc
        );

    if (!argv) {
        MessageBoxW(
            nullptr,
            L"Could not read command line.",
            kAppName,
            MB_OK | MB_ICONERROR
        );

        CloseHandle(
            single_instance
        );

        return 2;
    }


    const std::filesystem::path model_path =
        Paths::resolve_model_path(
            argc,
            argv
        );

    LocalFree(argv);


    if (
        model_path.empty() ||
        !std::filesystem::is_regular_file(
            model_path
        )
    ) {
        MessageBoxW(
            nullptr,

            L"ASR model was not found.\n\n"
            L"Use:\n"
            L"loqel.exe --model C:\\path\\model.gguf\n\n"
            L"or set NEMO_SPEECH_MODEL.\n\n"
            L"The app also searches models\\ for:\n"
            L"nemotron-speech-streaming-en-0.6b.q8_0.gguf\n"
            L"nemotron-speech-streaming-en-0.6b.new.q8_0.gguf",

            kAppName,
            MB_OK | MB_ICONERROR
        );

        CloseHandle(
            single_instance
        );

        return 3;
    }


    App app(
        instance,
        model_path
    );

    const int result =
        app.run();


    CloseHandle(
        single_instance
    );

    return result;
}

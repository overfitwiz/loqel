#include "FormattingSettings.h"

#include <windows.h>
#include <shlobj.h>

#include <system_error>

#include "windows/WindowsText.h"

namespace {

std::filesystem::path settings_path() {
    PWSTR local_app_data = nullptr;

    if (
        SUCCEEDED(
            SHGetKnownFolderPath(
                FOLDERID_LocalAppData,
                KF_FLAG_DEFAULT,
                nullptr,
                &local_app_data
            )
        ) &&
        local_app_data
    ) {
        std::filesystem::path path =
            std::filesystem::path(local_app_data) /
            L"NeMoTalk" /
            L"settings.ini";

        CoTaskMemFree(local_app_data);
        return path;
    }

    if (local_app_data) {
        CoTaskMemFree(local_app_data);
    }

    return L"settings.ini";
}

}

FormattingSettings::FormattingSettings()
    : path_(settings_path()) {
}

bool FormattingSettings::load(
    MarkdownCommands& commands,
    CustomDictionarySettings& dictionary,
    RecognitionSettings& recognition,
    std::string& error
) const {
    error.clear();
    commands = MarkdownCommands::defaults();
    dictionary = {};
    recognition = {};

    std::error_code filesystem_error;

    if (!std::filesystem::is_regular_file(path_, filesystem_error)) {
        return true;
    }

    for (std::size_t i = 0; i < kMarkdownCommandCount; ++i) {
        const auto command = static_cast<MarkdownCommand>(i);
        wchar_t value[512] = {};

        GetPrivateProfileStringW(
            L"markdown_triggers",
            WindowsText::from_utf8(markdown_command_key(command)).c_str(),
            WindowsText::from_utf8(commands.phrase(command)).c_str(),
            value,
            512,
            path_.c_str()
        );

        if (value[0] != L'\0') {
            commands.phrase(command) = WindowsText::to_utf8(value);
        }
    }

    const UINT phrase_count = std::min<UINT>(
        GetPrivateProfileIntW(
            L"custom_dictionary",
            L"count",
            0,
            path_.c_str()
        ),
        1000
    );

    for (UINT i = 0; i < phrase_count; ++i) {
        const std::wstring key =
            L"phrase_" + std::to_wstring(i);

        wchar_t value[512] = {};

        GetPrivateProfileStringW(
            L"custom_dictionary",
            key.c_str(),
            L"",
            value,
            512,
            path_.c_str()
        );

        if (value[0] != L'\0') {
            dictionary.phrases.emplace_back(WindowsText::to_utf8(value));
        }
    }

    wchar_t boost_text[64] = {};

    GetPrivateProfileStringW(
        L"custom_dictionary",
        L"boost",
        L"2.0",
        boost_text,
        64,
        path_.c_str()
    );

    wchar_t* boost_end = nullptr;
    const float boost = std::wcstof(boost_text, &boost_end);

    if (
        boost_end != boost_text &&
        boost >= 0.0f &&
        boost <= 5.0f
    ) {
        dictionary.boost = boost;
    }

    wchar_t mode[64] = {};
    GetPrivateProfileStringW(
        L"recognition",
        L"mode",
        L"streaming",
        mode,
        64,
        path_.c_str()
    );

    if (_wcsicmp(mode, L"record_then_transcribe") == 0) {
        recognition.mode = RecognitionMode::RecordThenTranscribe;
    }

    wchar_t latency[64] = {};
    GetPrivateProfileStringW(
        L"recognition",
        L"latency",
        L"balanced",
        latency,
        64,
        path_.c_str()
    );

    if (_wcsicmp(latency, L"low") == 0) {
        recognition.latency = LatencyPreset::Low;
    }
    else if (_wcsicmp(latency, L"highest_accuracy") == 0) {
        recognition.latency = LatencyPreset::HighestAccuracy;
    }

    return true;
}

bool FormattingSettings::save(
    const MarkdownCommands& commands,
    const CustomDictionarySettings& dictionary,
    const RecognitionSettings& recognition,
    std::string& error
) const {
    error.clear();

    std::error_code filesystem_error;

    std::filesystem::create_directories(
        path_.parent_path(),
        filesystem_error
    );

    if (filesystem_error) {
        error =
            "Could not create the settings directory: " +
            WindowsText::to_utf8(path_.parent_path().wstring());

        return false;
    }

    for (std::size_t i = 0; i < kMarkdownCommandCount; ++i) {
        const auto command = static_cast<MarkdownCommand>(i);

        if (
            !WritePrivateProfileStringW(
                L"markdown_triggers",
                WindowsText::from_utf8(markdown_command_key(command)).c_str(),
                WindowsText::from_utf8(commands.phrase(command)).c_str(),
                path_.c_str()
            )
        ) {
            error =
                "Could not save formatting settings to: " +
                WindowsText::to_utf8(path_.wstring());

            return false;
        }
    }

    if (
        !WritePrivateProfileStringW(
            L"custom_dictionary",
            L"count",
            std::to_wstring(dictionary.phrases.size()).c_str(),
            path_.c_str()
        ) ||
        !WritePrivateProfileStringW(
            L"custom_dictionary",
            L"boost",
            std::to_wstring(dictionary.boost).c_str(),
            path_.c_str()
        )
    ) {
        error =
            "Could not save the custom dictionary to: " +
            WindowsText::to_utf8(path_.wstring());

        return false;
    }

    for (std::size_t i = 0; i < dictionary.phrases.size(); ++i) {
        const std::wstring key =
            L"phrase_" + std::to_wstring(i);

        if (
            !WritePrivateProfileStringW(
                L"custom_dictionary",
                key.c_str(),
                WindowsText::from_utf8(dictionary.phrases[i]).c_str(),
                path_.c_str()
            )
        ) {
            error =
                "Could not save the custom dictionary to: " +
                WindowsText::to_utf8(path_.wstring());

            return false;
        }
    }

    const wchar_t* mode =
        recognition.mode == RecognitionMode::RecordThenTranscribe
            ? L"record_then_transcribe"
            : L"streaming";

    const wchar_t* latency = L"balanced";
    switch (recognition.latency) {
        case LatencyPreset::Low:
            latency = L"low";
            break;
        case LatencyPreset::Balanced:
            break;
        case LatencyPreset::HighestAccuracy:
            latency = L"highest_accuracy";
            break;
    }

    if (
        !WritePrivateProfileStringW(
            L"recognition",
            L"mode",
            mode,
            path_.c_str()
        ) ||
        !WritePrivateProfileStringW(
            L"recognition",
            L"latency",
            latency,
            path_.c_str()
        )
    ) {
        error =
            "Could not save recognition settings to: " +
            WindowsText::to_utf8(path_.wstring());
        return false;
    }

    return true;
}

const std::filesystem::path& FormattingSettings::path() const {
    return path_;
}

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
            L"loqel" /
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
    CustomDictionarySettings& dictionary,
    RecognitionSettings& recognition,
    CleanupSettings& cleanup,
    LlmSettings& llm,
    DebugSettings& debug,
    HotkeySettings& hotkeys,
    std::string& error
) const {
    error.clear();
    dictionary = {};
    recognition = {};
    cleanup = {};
    llm = {};
    debug = {};
    debug.output_directory = path_.parent_path() / L"debug";
    hotkeys = {};

    std::error_code filesystem_error;

    if (!std::filesystem::is_regular_file(path_, filesystem_error)) {
        return true;
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

    wchar_t cleanup_count_text[32] = {};
    GetPrivateProfileStringW(
        L"auto_cleanup",
        L"count",
        L"",
        cleanup_count_text,
        32,
        path_.c_str()
    );

    // A missing count means this is an older settings file, so retain the
    // defaults. A saved count of zero intentionally disables cleanup.
    if (cleanup_count_text[0] != L'\0') {
        cleanup.remove_words.clear();
        const UINT cleanup_count = std::min<UINT>(
            static_cast<UINT>(_wtoi(cleanup_count_text)),
            1000
        );

        for (UINT i = 0; i < cleanup_count; ++i) {
            const std::wstring key = L"word_" + std::to_wstring(i);
            wchar_t value[512] = {};

            GetPrivateProfileStringW(
                L"auto_cleanup",
                key.c_str(),
                L"",
                value,
                512,
                path_.c_str()
            );

            if (value[0] != L'\0') {
                cleanup.remove_words.emplace_back(
                    WindowsText::to_utf8(value)
                );
            }
        }
    }

    const int formatted_key = GetPrivateProfileIntW(
        L"hotkeys",
        L"formatted_function_key",
        hotkeys.formatted_function_key,
        path_.c_str()
    );
    const int plain_key = GetPrivateProfileIntW(
        L"hotkeys",
        L"plain_function_key",
        hotkeys.plain_function_key,
        path_.c_str()
    );

    if (
        formatted_key >= 1 && formatted_key <= 12 &&
        plain_key >= 1 && plain_key <= 12 &&
        formatted_key != plain_key
    ) {
        hotkeys.formatted_function_key = formatted_key;
        hotkeys.plain_function_key = plain_key;
    }

    llm.enabled = GetPrivateProfileIntW(
        L"llm_correction",
        L"enabled",
        llm.enabled ? 1 : 0,
        path_.c_str()
    ) != 0;

    debug.enabled = GetPrivateProfileIntW(
        L"debug",
        L"enabled",
        0,
        path_.c_str()
    ) != 0;

    return true;
}

bool FormattingSettings::save(
    const CustomDictionarySettings& dictionary,
    const RecognitionSettings& recognition,
    const CleanupSettings& cleanup,
    const LlmSettings& llm,
    const DebugSettings& debug,
    const HotkeySettings& hotkeys,
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

    if (!WritePrivateProfileStringW(
            L"auto_cleanup",
            L"count",
            std::to_wstring(cleanup.remove_words.size()).c_str(),
            path_.c_str()
        )) {
        error =
            "Could not save auto-cleanup settings to: " +
            WindowsText::to_utf8(path_.wstring());
        return false;
    }

    for (std::size_t i = 0; i < cleanup.remove_words.size(); ++i) {
        const std::wstring key = L"word_" + std::to_wstring(i);

        if (!WritePrivateProfileStringW(
                L"auto_cleanup",
                key.c_str(),
                WindowsText::from_utf8(cleanup.remove_words[i]).c_str(),
                path_.c_str()
            )) {
            error =
                "Could not save auto-cleanup settings to: " +
                WindowsText::to_utf8(path_.wstring());
            return false;
        }
    }

    if (
        !WritePrivateProfileStringW(
            L"hotkeys",
            L"formatted_function_key",
            std::to_wstring(hotkeys.formatted_function_key).c_str(),
            path_.c_str()
        ) ||
        !WritePrivateProfileStringW(
            L"hotkeys",
            L"plain_function_key",
            std::to_wstring(hotkeys.plain_function_key).c_str(),
            path_.c_str()
        )
    ) {
        error =
            "Could not save hotkey settings to: " +
            WindowsText::to_utf8(path_.wstring());
        return false;
    }

    if (!WritePrivateProfileStringW(
            L"llm_correction",
            L"enabled",
            llm.enabled ? L"1" : L"0",
            path_.c_str()
        )) {
        error =
            "Could not save LLM correction settings to: " +
            WindowsText::to_utf8(path_.wstring());
        return false;
    }

    if (!WritePrivateProfileStringW(
            L"debug",
            L"enabled",
            debug.enabled ? L"1" : L"0",
            path_.c_str()
        )) {
        error =
            "Could not save debug settings to: " +
            WindowsText::to_utf8(path_.wstring());
        return false;
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

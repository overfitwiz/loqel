#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "core/ApplicationSettings.h"

class FormattingSettings {
public:
    FormattingSettings();

    bool load(
        CustomDictionarySettings& dictionary,
        RecognitionSettings& recognition,
        CleanupSettings& cleanup,
        LlmSettings& llm,
        DebugSettings& debug,
        HotkeySettings& hotkeys,
        std::string& error
    ) const;

    bool save(
        const CustomDictionarySettings& dictionary,
        const RecognitionSettings& recognition,
        const CleanupSettings& cleanup,
        const LlmSettings& llm,
        const DebugSettings& debug,
        const HotkeySettings& hotkeys,
        std::string& error
    ) const;

    const std::filesystem::path& path() const;

private:
    std::filesystem::path path_;
};

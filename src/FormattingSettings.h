#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "MarkdownFormatter.h"
#include "core/ApplicationSettings.h"

class FormattingSettings {
public:
    FormattingSettings();

    bool load(
        MarkdownCommands& commands,
        CustomDictionarySettings& dictionary,
        RecognitionSettings& recognition,
        std::string& error
    ) const;

    bool save(
        const MarkdownCommands& commands,
        const CustomDictionarySettings& dictionary,
        const RecognitionSettings& recognition,
        std::string& error
    ) const;

    const std::filesystem::path& path() const;

private:
    std::filesystem::path path_;
};

#include "MarkdownFormatter.h"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace {

constexpr std::array<const char*, kMarkdownCommandCount> kLabels = {
    "List (-)",
    "Next item (-)",
    "Bold (**)",
    "Italic (*)",
    "Heading (#)",
    "Subheading (##)",
    "Paragraph"
};

constexpr std::array<const char*, kMarkdownCommandCount> kKeys = {
    "list",
    "item",
    "bold",
    "italic",
    "heading",
    "subheading",
    "paragraph"
};

constexpr std::array<const char*, kMarkdownCommandCount> kDefaults = {
    "list",
    "item",
    "bold",
    "italic",
    "heading",
    "subheading",
    "paragraph"
};

std::size_t index_of(MarkdownCommand command) {
    return static_cast<std::size_t>(command);
}

bool is_word_byte(unsigned char character) {
    return character >= 0x80 || std::isalnum(character) || character == '_';
}

bool is_space_byte(unsigned char character) {
    return character < 0x80 && std::isspace(character);
}

char ascii_lower(char character) {
    const auto byte = static_cast<unsigned char>(character);
    return byte < 0x80
        ? static_cast<char>(std::tolower(byte))
        : character;
}

bool equal_insensitive(
    std::string_view left,
    std::string_view right
) {
    if (left.size() != right.size()) {
        return false;
    }

    for (std::size_t i = 0; i < left.size(); ++i) {
        if (ascii_lower(left[i]) != ascii_lower(right[i])) {
            return false;
        }
    }

    return true;
}

bool matches_at(
    const std::string& text,
    std::size_t position,
    const std::string& phrase
) {
    if (phrase.empty() || position + phrase.size() > text.size()) {
        return false;
    }

    if (
        position > 0 &&
        is_word_byte(static_cast<unsigned char>(text[position - 1]))
    ) {
        return false;
    }

    const std::size_t end = position + phrase.size();
    if (
        end < text.size() &&
        is_word_byte(static_cast<unsigned char>(text[end]))
    ) {
        return false;
    }

    return equal_insensitive(
        std::string_view(text).substr(position, phrase.size()),
        phrase
    );
}

void trim_output_spaces(std::string& output) {
    while (!output.empty() && output.back() == ' ') {
        output.pop_back();
    }
}

void ensure_block_break(std::string& output) {
    trim_output_spaces(output);

    while (!output.empty() && output.back() == '\n') {
        output.pop_back();
    }

    if (!output.empty()) {
        output += "\n\n";
    }
}

void append_plain(
    std::string& output,
    std::string_view text
) {
    bool pending_space = false;

    for (char character : text) {
        if (is_space_byte(static_cast<unsigned char>(character))) {
            pending_space = true;
            continue;
        }

        if (
            pending_space &&
            !output.empty() &&
            output.back() != ' ' &&
            output.back() != '\n' &&
            character != '.' &&
            character != ',' &&
            character != ';' &&
            character != ':' &&
            character != '!' &&
            character != '?'
        ) {
            output.push_back(' ');
        }

        output.push_back(character);
        pending_space = false;
    }

    if (pending_space && !output.empty() && output.back() != '\n') {
        output.push_back(' ');
    }
}

bool consumes_following_punctuation(MarkdownCommand command) {
    return
        command == MarkdownCommand::List ||
        command == MarkdownCommand::NextItem ||
        command == MarkdownCommand::Heading ||
        command == MarkdownCommand::Subheading ||
        command == MarkdownCommand::NewParagraph;
}

bool is_separator_punctuation(char character) {
    return
        character == '.' ||
        character == ',' ||
        character == ':' ||
        character == ';';
}

}

MarkdownCommands MarkdownCommands::defaults() {
    MarkdownCommands commands;

    for (std::size_t i = 0; i < kMarkdownCommandCount; ++i) {
        commands.phrases[i] = kDefaults[i];
    }

    return commands;
}

std::string& MarkdownCommands::phrase(MarkdownCommand command) {
    return phrases[index_of(command)];
}

const std::string& MarkdownCommands::phrase(
    MarkdownCommand command
) const {
    return phrases[index_of(command)];
}

const char* markdown_command_label(MarkdownCommand command) {
    return kLabels[index_of(command)];
}

const char* markdown_command_key(MarkdownCommand command) {
    return kKeys[index_of(command)];
}

std::string MarkdownFormatter::format(
    const std::string& transcript,
    const MarkdownCommands& commands
) {
    std::string output;
    output.reserve(transcript.size() + 32);

    bool bold_open = false;
    bool italic_open = false;
    std::size_t plain_start = 0;
    std::size_t position = 0;

    while (position < transcript.size()) {
        bool found = false;
        MarkdownCommand matched = MarkdownCommand::Count;
        std::size_t matched_size = 0;

        for (std::size_t i = 0; i < kMarkdownCommandCount; ++i) {
            const auto command = static_cast<MarkdownCommand>(i);
            const std::string& phrase = commands.phrase(command);

            if (
                !phrase.empty() &&
                phrase.size() > matched_size &&
                matches_at(transcript, position, phrase)
            ) {
                found = true;
                matched = command;
                matched_size = phrase.size();
            }
        }

        if (!found) {
            ++position;
            continue;
        }

        append_plain(
            output,
            std::string_view(transcript).substr(
                plain_start,
                position - plain_start
            )
        );

        const bool starts_inline_style =
            (matched == MarkdownCommand::Bold && !bold_open) ||
            (matched == MarkdownCommand::Italic && !italic_open);

        const bool ends_inline_style =
            (matched == MarkdownCommand::Bold && bold_open) ||
            (matched == MarkdownCommand::Italic && italic_open);

        if (!starts_inline_style) {
            trim_output_spaces(output);
        }

        switch (matched) {
            case MarkdownCommand::List:
            case MarkdownCommand::NextItem:
                trim_output_spaces(output);
                if (!output.empty()) {
                    output += '\n';
                }
                output += "- ";
                break;

            case MarkdownCommand::Bold:
                output += "**";
                bold_open = !bold_open;
                break;

            case MarkdownCommand::Italic:
                output += '*';
                italic_open = !italic_open;
                break;

            case MarkdownCommand::Heading:
                ensure_block_break(output);
                output += "# ";
                break;

            case MarkdownCommand::Subheading:
                ensure_block_break(output);
                output += "## ";
                break;

            case MarkdownCommand::NewParagraph:
                ensure_block_break(output);
                break;

            case MarkdownCommand::Count:
                break;
        }

        position += matched_size;
        bool had_following_space = false;

        while (
            position < transcript.size() &&
            is_space_byte(static_cast<unsigned char>(transcript[position]))
        ) {
            had_following_space = true;
            ++position;
        }

        if (
            (consumes_following_punctuation(matched) || starts_inline_style) &&
            position < transcript.size() &&
            is_separator_punctuation(transcript[position])
        ) {
            ++position;

            while (
                position < transcript.size() &&
                is_space_byte(static_cast<unsigned char>(transcript[position]))
            ) {
                ++position;
            }
        }

        if (
            ends_inline_style &&
            had_following_space &&
            position < transcript.size() &&
            !is_separator_punctuation(transcript[position]) &&
            transcript[position] != '!' &&
            transcript[position] != '?'
        ) {
            output.push_back(' ');
        }

        plain_start = position;
    }

    append_plain(output, std::string_view(transcript).substr(plain_start));
    trim_output_spaces(output);

    while (!output.empty() && output.back() == '\n') {
        output.pop_back();
    }

    return output;
}

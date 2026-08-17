#pragma once

#include <array>
#include <cstddef>
#include <string>

enum class MarkdownCommand : std::size_t {
    List,
    NextItem,
    Bold,
    Italic,
    Heading,
    Subheading,
    NewParagraph,
    Count
};

constexpr std::size_t kMarkdownCommandCount =
    static_cast<std::size_t>(MarkdownCommand::Count);

struct MarkdownCommands {
    std::array<std::string, kMarkdownCommandCount> phrases;

    static MarkdownCommands defaults();

    std::string& phrase(MarkdownCommand command);
    const std::string& phrase(MarkdownCommand command) const;
};

const char* markdown_command_label(MarkdownCommand command);
const char* markdown_command_key(MarkdownCommand command);

class MarkdownFormatter {
public:
    static std::string format(
        const std::string& transcript,
        const MarkdownCommands& commands
    );
};

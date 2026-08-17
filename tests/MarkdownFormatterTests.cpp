#include "MarkdownFormatter.h"

#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(
    const char* name,
    const std::string& actual,
    const std::string& expected
) {
    if (actual == expected) {
        return;
    }

    std::cerr
        << "FAILED: " << name << "\n"
        << "expected: [" << expected << "]\n"
        << "actual:   [" << actual << "]\n";

    ++failures;
}

}

int main() {
    const MarkdownCommands commands = MarkdownCommands::defaults();

    expect(
        "unordered list",
        MarkdownFormatter::format(
            "list apples list bananas",
            commands
        ),
        "- apples\n- bananas"
    );

    expect(
        "item always inserts its marker",
        MarkdownFormatter::format(
            "item apples item bananas",
            commands
        ),
        "- apples\n- bananas"
    );

    expect(
        "inline styles",
        MarkdownFormatter::format(
            "Use bold strong bold and italic emphasis italic.",
            commands
        ),
        "Use **strong** and *emphasis*."
    );

    expect(
        "headings and paragraphs",
        MarkdownFormatter::format(
            "heading release notes paragraph subheading changes paragraph ready",
            commands
        ),
        "# release notes\n\n## changes\n\nready"
    );

    expect(
        "every punctuated list trigger inserts a marker",
        MarkdownFormatter::format(
            "List. Apples. Item, bananas. List.",
            commands
        ),
        "- Apples.\n- bananas.\n-"
    );

    MarkdownCommands customized = commands;
    customized.phrase(MarkdownCommand::NewParagraph) = "break here";
    customized.phrase(MarkdownCommand::Bold) = "strong";

    expect(
        "custom phrase",
        MarkdownFormatter::format(
            "First thought break here strong important strong",
            customized
        ),
        "First thought\n\n**important**"
    );

    expect(
        "single bold inserts one marker",
        MarkdownFormatter::format(
            "bold unfinished",
            commands
        ),
        "**unfinished"
    );

    expect(
        "single italic inserts one marker",
        MarkdownFormatter::format(
            "italic unfinished",
            commands
        ),
        "*unfinished"
    );

    expect(
        "closing style keeps sentence punctuation",
        MarkdownFormatter::format(
            "bold important bold.",
            commands
        ),
        "**important**."
    );

    expect(
        "UTF-8 text is preserved",
        MarkdownFormatter::format(
            u8"heading Résumé — 東京",
            commands
        ),
        u8"# Résumé — 東京"
    );

    return failures == 0 ? 0 : 1;
}

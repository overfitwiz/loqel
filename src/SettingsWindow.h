#pragma once

#include <windows.h>

#include <array>

#include "FormattingSettings.h"

class SettingsWindow {
public:
    static constexpr UINT kSavedMessage = WM_APP + 6;

    ~SettingsWindow();

    bool show(
        HINSTANCE instance,
        HWND owner,
        const MarkdownCommands& commands,
        const CustomDictionarySettings& dictionary,
        const RecognitionSettings& recognition,
        const CleanupSettings& cleanup,
        const HotkeySettings& hotkeys
    );

    const MarkdownCommands& commands() const;
    const CustomDictionarySettings& dictionary() const;
    const RecognitionSettings& recognition() const;
    const CleanupSettings& cleanup() const;
    const HotkeySettings& hotkeys() const;
    void destroy();

private:
    bool create(HINSTANCE instance, HWND owner);
    void populate(
        const MarkdownCommands& commands,
        const CustomDictionarySettings& dictionary,
        const RecognitionSettings& recognition,
        const CleanupSettings& cleanup,
        const HotkeySettings& hotkeys
    );
    void save_from_controls();
    void center_on_owner();

    static LRESULT CALLBACK window_proc(
        HWND window,
        UINT message,
        WPARAM wparam,
        LPARAM lparam
    );

    HWND window_ = nullptr;
    HWND owner_ = nullptr;

    std::array<HWND, kMarkdownCommandCount> edits_ = {};
    HWND dictionary_edit_ = nullptr;
    HWND boost_edit_ = nullptr;
    HWND mode_combo_ = nullptr;
    HWND latency_combo_ = nullptr;
    HWND cleanup_edit_ = nullptr;
    HWND formatted_hotkey_combo_ = nullptr;
    HWND plain_hotkey_combo_ = nullptr;

    MarkdownCommands commands_ = MarkdownCommands::defaults();
    CustomDictionarySettings dictionary_;
    RecognitionSettings recognition_;
    CleanupSettings cleanup_;
    HotkeySettings hotkeys_;
};

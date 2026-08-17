#pragma once

#include <windows.h>

#include "core/ApplicationSettings.h"

class Hotkey {
public:
    ~Hotkey();

    static constexpr UINT kDownMessage = WM_APP + 2;
    static constexpr UINT kUpMessage = WM_APP + 3;
    static constexpr WPARAM kFormattedMode = 0;
    static constexpr WPARAM kPlainMode = 1;

    bool install(
        HINSTANCE instance,
        HWND window,
        const HotkeySettings& settings
    );
    void configure(const HotkeySettings& settings);
    void remove();

private:
    static LRESULT CALLBACK keyboard_proc(
        int code,
        WPARAM message,
        LPARAM lparam
    );

    HWND window_ = nullptr;
    HHOOK hook_ = nullptr;
    DWORD formatted_key_ = VK_F8;
    DWORD plain_key_ = VK_F9;
    DWORD active_key_ = 0;

    static inline Hotkey* owner_ = nullptr;
};

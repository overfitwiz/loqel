#pragma once

#include <windows.h>

class Hotkey {
public:
    ~Hotkey();

    static constexpr UINT kDownMessage = WM_APP + 2;
    static constexpr UINT kUpMessage = WM_APP + 3;

    bool install(HINSTANCE instance, HWND window);
    void remove();

private:
    static LRESULT CALLBACK keyboard_proc(
        int code,
        WPARAM message,
        LPARAM lparam
    );

    HWND window_ = nullptr;
    HHOOK hook_ = nullptr;
    bool key_down_ = false;

    static inline Hotkey* owner_ = nullptr;
};

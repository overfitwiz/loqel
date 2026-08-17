#pragma once

#include <windows.h>

class TrayIcon {
public:
    static constexpr UINT kTrayMessage = WM_APP + 1;
    static constexpr UINT kSettingsCommand = 100;
    static constexpr UINT kExitCommand = 101;

    ~TrayIcon();

    bool create(HWND window);
    void remove();

    void show_menu(HWND window);

private:
    NOTIFYICONDATAW data_ = {};
    bool created_ = false;
};

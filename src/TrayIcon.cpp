#include "TrayIcon.h"

#include <shellapi.h>

#include "resource.h"

namespace {

constexpr UINT kTrayId = 1;

}

TrayIcon::~TrayIcon() {
    remove();
}

bool TrayIcon::create(HWND window) {
    data_ = {};

    data_.cbSize = sizeof(data_);
    data_.hWnd = window;
    data_.uID = kTrayId;

    data_.uFlags =
        NIF_MESSAGE |
        NIF_ICON |
        NIF_TIP;

    data_.uCallbackMessage = kTrayMessage;
    data_.hIcon = LoadIconW(
        GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDI_LOQEL_APP)
    );
    if (!data_.hIcon) {
        data_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }

    wcscpy_s(
        data_.szTip,
        L"Loqel"
    );

    created_ =
        Shell_NotifyIconW(
            NIM_ADD,
            &data_
        );

    return created_;
}

void TrayIcon::remove() {
    if (!created_) {
        return;
    }

    Shell_NotifyIconW(
        NIM_DELETE,
        &data_
    );

    created_ = false;
}

void TrayIcon::show_menu(HWND window) {
    POINT cursor_position = {};

    GetCursorPos(&cursor_position);

    HMENU menu = CreatePopupMenu();

    AppendMenuW(
        menu,
        MF_STRING,
        kSettingsCommand,
        L"Settings..."
    );

    AppendMenuW(
        menu,
        MF_SEPARATOR,
        0,
        nullptr
    );

    AppendMenuW(
        menu,
        MF_STRING,
        kExitCommand,
        L"Exit"
    );

    SetForegroundWindow(window);

    TrackPopupMenu(
        menu,
        TPM_RIGHTBUTTON,
        cursor_position.x,
        cursor_position.y,
        0,
        window,
        nullptr
    );

    DestroyMenu(menu);
}

#include "Hotkey.h"

Hotkey::~Hotkey() {
    remove();
}

bool Hotkey::install(
    HINSTANCE instance,
    HWND window,
    const HotkeySettings& settings
) {
    window_ = window;
    owner_ = this;
    configure(settings);

    hook_ = SetWindowsHookExW(
        WH_KEYBOARD_LL,
        &Hotkey::keyboard_proc,
        instance,
        0
    );

    if (!hook_) {
        owner_ = nullptr;
        window_ = nullptr;
        return false;
    }

    return true;
}

void Hotkey::configure(const HotkeySettings& settings) {
    formatted_key_ = static_cast<DWORD>(
        VK_F1 + settings.formatted_function_key - 1
    );
    plain_key_ = static_cast<DWORD>(
        VK_F1 + settings.plain_function_key - 1
    );
}

void Hotkey::remove() {
    if (hook_) {
        UnhookWindowsHookEx(hook_);
        hook_ = nullptr;
    }

    if (owner_ == this) {
        owner_ = nullptr;
    }

    window_ = nullptr;
    active_key_ = 0;
}

LRESULT CALLBACK Hotkey::keyboard_proc(
    int code,
    WPARAM message,
    LPARAM lparam
) {
    if (code == HC_ACTION && owner_) {
        const auto* key =
            reinterpret_cast<const KBDLLHOOKSTRUCT*>(lparam);

        if (
            (key->vkCode == owner_->formatted_key_ ||
             key->vkCode == owner_->plain_key_ ||
             key->vkCode == owner_->active_key_) &&
            !(key->flags & LLKHF_INJECTED)
        ) {
            const bool down =
                message == WM_KEYDOWN ||
                message == WM_SYSKEYDOWN;

            const bool up =
                message == WM_KEYUP ||
                message == WM_SYSKEYUP;

            if (down && owner_->active_key_ == 0) {
                owner_->active_key_ = key->vkCode;

                PostMessageW(
                    owner_->window_,
                    kDownMessage,
                    key->vkCode == owner_->plain_key_
                        ? kPlainMode
                        : kFormattedMode,
                    0
                );
            }

            if (up && key->vkCode == owner_->active_key_) {
                owner_->active_key_ = 0;

                PostMessageW(
                    owner_->window_,
                    kUpMessage,
                    0,
                    0
                );
            }

            // Prevent configured dictation keys from reaching the foreground
            // application.
            return 1;
        }
    }

    return CallNextHookEx(
        nullptr,
        code,
        message,
        lparam
    );
}

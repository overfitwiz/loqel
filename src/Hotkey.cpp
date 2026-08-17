#include "Hotkey.h"

namespace {

constexpr DWORD kTriggerKey = VK_F8;

}

Hotkey::~Hotkey() {
    remove();
}

bool Hotkey::install(HINSTANCE instance, HWND window) {
    window_ = window;
    owner_ = this;

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

void Hotkey::remove() {
    if (hook_) {
        UnhookWindowsHookEx(hook_);
        hook_ = nullptr;
    }

    if (owner_ == this) {
        owner_ = nullptr;
    }

    window_ = nullptr;
    key_down_ = false;
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
            key->vkCode == kTriggerKey &&
            !(key->flags & LLKHF_INJECTED)
        ) {
            const bool down =
                message == WM_KEYDOWN ||
                message == WM_SYSKEYDOWN;

            const bool up =
                message == WM_KEYUP ||
                message == WM_SYSKEYUP;

            if (down && !owner_->key_down_) {
                owner_->key_down_ = true;

                PostMessageW(
                    owner_->window_,
                    kDownMessage,
                    0,
                    0
                );
            }

            if (up && owner_->key_down_) {
                owner_->key_down_ = false;

                PostMessageW(
                    owner_->window_,
                    kUpMessage,
                    0,
                    0
                );
            }

            // Prevent F8 from reaching the foreground application.
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
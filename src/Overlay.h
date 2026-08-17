#pragma once

#include <windows.h>

#include <string>

class Overlay {
public:
    ~Overlay();

    bool create(HINSTANCE instance, HWND owner);

    void show(const std::wstring& text);
    void hide();
    void destroy();

private:
    static LRESULT CALLBACK window_proc(
        HWND window,
        UINT message,
        WPARAM wparam,
        LPARAM lparam
    );

    HWND window_ = nullptr;
    std::wstring text_;
};

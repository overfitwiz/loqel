#include "Overlay.h"

namespace {

constexpr wchar_t kOverlayClassName[] =
    L"SpeechAppOverlayWindow";

constexpr int kOverlayWidth = 720;
constexpr int kOverlayHeight = 190;

}

Overlay::~Overlay() {
    destroy();
}

bool Overlay::create(
    HINSTANCE instance,
    HWND owner
) {
    WNDCLASSW window_class = {};

    window_class.lpfnWndProc = &Overlay::window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = kOverlayClassName;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);

    if (
        !RegisterClassW(&window_class) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS
    ) {
        return false;
    }

    window_ = CreateWindowExW(
        WS_EX_TOPMOST |
            WS_EX_TOOLWINDOW |
            WS_EX_NOACTIVATE |
            WS_EX_LAYERED,

        kOverlayClassName,
        L"Speech App",

        WS_POPUP,

        0,
        0,
        kOverlayWidth,
        kOverlayHeight,

        owner,
        nullptr,
        instance,
        this
    );

    if (!window_) {
        return false;
    }

    SetLayeredWindowAttributes(
        window_,
        0,
        235,
        LWA_ALPHA
    );

    return true;
}

void Overlay::show(const std::wstring& text) {
    text_ = text;

    RECT work_area = {};

    SystemParametersInfoW(
        SPI_GETWORKAREA,
        0,
        &work_area,
        0
    );

    const int x =
        work_area.left +
        ((work_area.right - work_area.left) - kOverlayWidth) / 2;

    const int y =
        work_area.bottom -
        kOverlayHeight -
        40;

    SetWindowPos(
        window_,
        HWND_TOPMOST,
        x,
        y,
        kOverlayWidth,
        kOverlayHeight,
        SWP_NOACTIVATE | SWP_SHOWWINDOW
    );

    InvalidateRect(
        window_,
        nullptr,
        TRUE
    );
}

void Overlay::hide() {
    ShowWindow(
        window_,
        SW_HIDE
    );
}

void Overlay::destroy() {
    if (window_) {
        DestroyWindow(window_);
        window_ = nullptr;
    }
}

LRESULT CALLBACK Overlay::window_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam
) {
    Overlay* overlay =
        reinterpret_cast<Overlay*>(
            GetWindowLongPtrW(
                window,
                GWLP_USERDATA
            )
        );

    if (message == WM_NCCREATE) {
        auto* create_info =
            reinterpret_cast<CREATESTRUCTW*>(lparam);

        overlay =
            static_cast<Overlay*>(
                create_info->lpCreateParams
            );

        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(overlay)
        );
    }

    switch (message) {
        case WM_NCHITTEST:
            return HTTRANSPARENT;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT paint = {};

            HDC dc =
                BeginPaint(
                    window,
                    &paint
                );

            RECT rect = {};

            GetClientRect(
                window,
                &rect
            );

            HBRUSH background =
                CreateSolidBrush(
                    RGB(28, 30, 34)
                );

            FillRect(
                dc,
                &rect,
                background
            );

            DeleteObject(background);

            SetBkMode(
                dc,
                TRANSPARENT
            );

            SetTextColor(
                dc,
                RGB(245, 247, 250)
            );

            HFONT font =
                CreateFontW(
                    24,
                    0,
                    0,
                    0,
                    FW_NORMAL,
                    FALSE,
                    FALSE,
                    FALSE,
                    DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY,
                    DEFAULT_PITCH,
                    L"Segoe UI"
                );

            HGDIOBJ old_font =
                SelectObject(
                    dc,
                    font
                );

            if (overlay) {
                RECT text_rect = rect;
                InflateRect(&text_rect, -24, -18);

                DrawTextW(
                    dc,
                    overlay->text_.c_str(),
                    static_cast<int>(
                        overlay->text_.size()
                    ),
                    &text_rect,
                    DT_LEFT |
                        DT_TOP |
                        DT_WORDBREAK |
                        DT_EDITCONTROL
                );
            }

            SelectObject(
                dc,
                old_font
            );

            DeleteObject(font);

            EndPaint(
                window,
                &paint
            );

            return 0;
        }

        default:
            return DefWindowProcW(
                window,
                message,
                wparam,
                lparam
            );
    }
}

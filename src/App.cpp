#include "App.h"

#include <filesystem>
#include <memory>
#include <utility>

#include "resource.h"
#include "windows/WindowsText.h"

namespace {

constexpr wchar_t kWindowClassName[] = L"NeMoTalkMainWindow";

UINT message_box_icon(MessageKind kind) {
    switch (kind) {
        case MessageKind::Information:
            return MB_ICONINFORMATION;
        case MessageKind::Warning:
            return MB_ICONWARNING;
        case MessageKind::Error:
            return MB_ICONERROR;
    }

    return 0;
}

}

App::App(
    HINSTANCE instance,
    std::filesystem::path model_path
)
    : instance_(instance),
      model_path_(std::move(model_path)),
      core_(*this, audio_capture_) {
}

App::~App() {
    shutdown();
}

bool App::create_window() {
    WNDCLASSW window_class = {};
    window_class.lpfnWndProc = &App::window_proc;
    window_class.hInstance = instance_;
    window_class.lpszClassName = kWindowClassName;
    window_class.hIcon = LoadIconW(
        instance_,
        MAKEINTRESOURCEW(IDI_LOQEL_APP)
    );
    if (!window_class.hIcon) {
        window_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }

    if (
        !RegisterClassW(&window_class) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS
    ) {
        return false;
    }

    window_ = CreateWindowExW(
        0,
        kWindowClassName,
        L"NeMo Talk",
        0,
        0,
        0,
        0,
        0,
        nullptr,
        nullptr,
        instance_,
        this
    );

    return window_ != nullptr;
}

int App::run() {
    if (!create_window()) {
        show_message(
            "NeMo Talk",
            "Could not create the application window.",
            MessageKind::Error
        );
        return 1;
    }

    std::string settings_error;
    if (!formatting_settings_.load(
            markdown_commands_,
            custom_dictionary_,
            recognition_settings_,
            cleanup_settings_,
            hotkey_settings_,
            settings_error
        )) {
        show_message(
            "Could not load settings",
            settings_error,
            MessageKind::Warning
        );
    }

    core_.set_settings(
        markdown_commands_,
        custom_dictionary_,
        recognition_settings_,
        cleanup_settings_
    );

    if (!tray_icon_.create(window_)) {
        show_message(
            "NeMo Talk",
            "Could not create the notification-area icon.",
            MessageKind::Error
        );
        return 2;
    }

    if (!overlay_.create(instance_, window_)) {
        show_message(
            "NeMo Talk",
            "Could not create the transcript overlay.",
            MessageKind::Error
        );
        return 3;
    }

    if (!std::filesystem::is_regular_file(model_path_)) {
        show_message(
            "NeMo Talk",
            "Model not found: " + WindowsText::to_utf8(model_path_.wstring()),
            MessageKind::Error
        );
        return 4;
    }

    std::string asr_error;
    if (!core_.initialize(model_path_, asr_error)) {
        show_message("Could not load NeMo", asr_error, MessageKind::Error);
        return 5;
    }

    if (!hotkey_.install(instance_, window_, hotkey_settings_)) {
        show_message(
            "NeMo Talk",
            "Could not install the global dictation hotkeys.",
            MessageKind::Error
        );
        return 6;
    }

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    shutdown();
    return static_cast<int>(message.wParam);
}

ActiveTarget App::capture_active_target() const {
    return reinterpret_cast<ActiveTarget>(GetForegroundWindow());
}

bool App::is_target_active(ActiveTarget target) const {
    const HWND window = reinterpret_cast<HWND>(target);
    return window && IsWindow(window) && GetForegroundWindow() == window;
}

void App::show_overlay(const std::string& text) {
    overlay_.show(WindowsText::from_utf8(text));
}

void App::hide_overlay() {
    overlay_.hide();
}

void App::show_message(
    const std::string& title,
    const std::string& text,
    MessageKind kind
) {
    const std::wstring wide_title = WindowsText::from_utf8(title);
    const std::wstring wide_text = WindowsText::from_utf8(text);

    MessageBoxW(
        window_,
        wide_text.c_str(),
        wide_title.c_str(),
        MB_OK | message_box_icon(kind)
    );
}

bool App::insert_text(
    const std::string& text,
    std::string& error
) {
    if (text_injector_.insert(WindowsText::from_utf8(text))) {
        return true;
    }

    error =
        "Windows did not accept the recognized text. Elevated applications "
        "cannot accept input from a non-elevated application.";
    return false;
}

bool App::post_to_main(std::function<void()> callback) {
    if (shutting_down_) {
        return false;
    }

    auto task = std::make_unique<std::function<void()>>(std::move(callback));

    if (!PostMessageW(
            window_,
            kRunTaskMessage,
            0,
            reinterpret_cast<LPARAM>(task.get())
        )) {
        return false;
    }

    task.release();
    return true;
}

void App::discard_pending_tasks() {
    if (!window_) {
        return;
    }

    MSG pending = {};
    while (PeekMessageW(
        &pending,
        window_,
        kRunTaskMessage,
        kRunTaskMessage,
        PM_REMOVE
    )) {
        delete reinterpret_cast<std::function<void()>*>(pending.lParam);
    }
}

void App::shutdown() {
    if (shutting_down_.exchange(true)) {
        return;
    }

    hotkey_.remove();
    core_.shutdown();
    discard_pending_tasks();
    settings_window_.destroy();
    overlay_.destroy();
    tray_icon_.remove();
}

LRESULT CALLBACK App::window_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam
) {
    App* app = reinterpret_cast<App*>(
        GetWindowLongPtrW(window, GWLP_USERDATA)
    );

    if (message == WM_NCCREATE) {
        const auto* create_info = reinterpret_cast<CREATESTRUCTW*>(lparam);
        app = static_cast<App*>(create_info->lpCreateParams);
        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(app)
        );
    }

    if (!app) {
        return DefWindowProcW(window, message, wparam, lparam);
    }

    switch (message) {
        case Hotkey::kDownMessage:
            app->core_.start_session(
                wparam == Hotkey::kPlainMode
                    ? OutputMode::Plain
                    : OutputMode::Formatted
            );
            return 0;

        case Hotkey::kUpMessage:
            app->core_.stop_session();
            return 0;

        case kRunTaskMessage: {
            std::unique_ptr<std::function<void()>> task(
                reinterpret_cast<std::function<void()>*>(lparam)
            );
            (*task)();
            return 0;
        }

        case TrayIcon::kTrayMessage:
            if (lparam == WM_RBUTTONUP || lparam == WM_LBUTTONUP) {
                app->tray_icon_.show_menu(window);
            }
            return 0;

        case WM_COMMAND:
            if (LOWORD(wparam) == TrayIcon::kSettingsCommand) {
                if (!app->settings_window_.show(
                        app->instance_,
                        window,
                        app->markdown_commands_,
                        app->custom_dictionary_,
                        app->recognition_settings_,
                        app->cleanup_settings_,
                        app->hotkey_settings_
                    )) {
                    app->show_message(
                        "NeMo Talk",
                        "Could not open settings.",
                        MessageKind::Error
                    );
                }
                return 0;
            }

            if (LOWORD(wparam) == TrayIcon::kExitCommand) {
                app->shutdown();
                DestroyWindow(window);
            }
            return 0;

        case SettingsWindow::kSavedMessage: {
            app->markdown_commands_ = app->settings_window_.commands();
            app->custom_dictionary_ = app->settings_window_.dictionary();
            app->recognition_settings_ = app->settings_window_.recognition();
            app->cleanup_settings_ = app->settings_window_.cleanup();
            app->hotkey_settings_ = app->settings_window_.hotkeys();
            app->hotkey_.configure(app->hotkey_settings_);
            app->core_.set_settings(
                app->markdown_commands_,
                app->custom_dictionary_,
                app->recognition_settings_,
                app->cleanup_settings_
            );

            std::string error;
            if (!app->formatting_settings_.save(
                    app->markdown_commands_,
                    app->custom_dictionary_,
                    app->recognition_settings_,
                    app->cleanup_settings_,
                    app->hotkey_settings_,
                    error
                )) {
                app->show_message(
                    "Could not save settings",
                    error,
                    MessageKind::Error
                );
            }
            return 0;
        }

        case WM_CLOSE:
            app->shutdown();
            DestroyWindow(window);
            return 0;

        case WM_DESTROY:
            app->window_ = nullptr;
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(window, message, wparam, lparam);
    }
}

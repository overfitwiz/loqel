#pragma once

#include <windows.h>

#include <atomic>
#include <filesystem>
#include <functional>

#include "AudioCapture.h"
#include "FormattingSettings.h"
#include "Hotkey.h"
#include "Overlay.h"
#include "SettingsWindow.h"
#include "TextInjector.h"
#include "TrayIcon.h"
#include "core/ApplicationCore.h"
#include "core/ApplicationPlatform.h"

class App final : public IApplicationPlatform {
public:
    App(HINSTANCE instance, std::filesystem::path model_path);
    ~App() override;

    int run();

    ActiveTarget capture_active_target() const override;
    bool is_target_active(ActiveTarget target) const override;
    void show_overlay(const std::string& text) override;
    void hide_overlay() override;
    void show_message(
        const std::string& title,
        const std::string& text,
        MessageKind kind
    ) override;
    bool insert_text(
        const std::string& text,
        std::string& error
    ) override;
    bool post_to_main(std::function<void()> callback) override;

private:
    static constexpr UINT kRunTaskMessage = WM_APP + 4;

    bool create_window();
    void shutdown();
    void discard_pending_tasks();

    static LRESULT CALLBACK window_proc(
        HWND window,
        UINT message,
        WPARAM wparam,
        LPARAM lparam
    );

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    std::filesystem::path model_path_;

    TrayIcon tray_icon_;
    Hotkey hotkey_;
    Overlay overlay_;
    SettingsWindow settings_window_;
    TextInjector text_injector_;
    FormattingSettings formatting_settings_;
    MarkdownCommands markdown_commands_ = MarkdownCommands::defaults();
    CustomDictionarySettings custom_dictionary_;
    RecognitionSettings recognition_settings_;

    WindowsAudioCapture audio_capture_;
    ApplicationCore core_;
    std::atomic<bool> shutting_down_{false};
};

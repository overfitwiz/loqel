#include "SettingsWindow.h"

#include <algorithm>
#include <cwctype>
#include <sstream>
#include <string>

#include "resource.h"
#include "windows/WindowsText.h"

namespace {

constexpr wchar_t kSettingsClassName[] =
    L"loqelFormattingSettingsWindow";

constexpr int kSaveButton = 2001;
constexpr int kCancelButton = 2002;
constexpr int kNavigationBase = 5000;
constexpr int kWindowWidth = 900;
constexpr int kWindowHeight = 620;
constexpr int kSidebarWidth = 205;

enum SettingsTab : std::size_t {
    GeneralTab,
    HotkeysTab,
    DictionaryTab,
    CleanupTab,
    MarkdownTab,
    SettingsTabCount
};

constexpr std::array<const wchar_t*, SettingsTabCount> kTabNames = {
    L"General",
    L"Hotkeys",
    L"Custom dictionary",
    L"Remove words",
    L"Markdown triggers"
};

constexpr COLORREF kSidebarColor = RGB(25, 28, 35);
constexpr COLORREF kSidebarSelectedColor = RGB(43, 48, 59);
constexpr COLORREF kSurfaceColor = RGB(247, 248, 250);
constexpr COLORREF kTextColor = RGB(30, 34, 42);
constexpr COLORREF kMutedColor = RGB(106, 114, 126);
constexpr COLORREF kAccentColor = RGB(75, 112, 232);

std::wstring control_text(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');

    if (length > 0) {
        GetWindowTextW(control, text.data(), length + 1);
    }

    text.resize(static_cast<std::size_t>(length));

    const auto first = std::find_if_not(
        text.begin(),
        text.end(),
        [](wchar_t character) { return std::iswspace(character); }
    );

    const auto last = std::find_if_not(
        text.rbegin(),
        text.rend(),
        [](wchar_t character) { return std::iswspace(character); }
    ).base();

    if (first >= last) {
        return {};
    }

    return std::wstring(first, last);
}

std::wstring lowercase(std::wstring text) {
    std::transform(
        text.begin(),
        text.end(),
        text.begin(),
        [](wchar_t character) { return std::towlower(character); }
    );

    return text;
}

std::vector<std::string> line_phrases(HWND control) {
    std::vector<std::string> phrases;
    std::wistringstream lines(control_text(control));
    std::wstring line;

    while (std::getline(lines, line)) {
        const auto first = std::find_if_not(
            line.begin(),
            line.end(),
            [](wchar_t character) { return std::iswspace(character); }
        );

        const auto last = std::find_if_not(
            line.rbegin(),
            line.rend(),
            [](wchar_t character) { return std::iswspace(character); }
        ).base();

        if (first < last) {
            phrases.emplace_back(
                WindowsText::to_utf8(std::wstring(first, last))
            );
        }
    }

    return phrases;
}

}

SettingsWindow::~SettingsWindow() {
    destroy();
}

bool SettingsWindow::create(
    HINSTANCE instance,
    HWND owner
) {
    WNDCLASSW window_class = {};
    window_class.lpfnWndProc = &SettingsWindow::window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = kSettingsClassName;
    window_class.hIcon = LoadIconW(
        instance,
        MAKEINTRESOURCEW(IDI_LOQEL_APP)
    );
    if (!window_class.hIcon) {
        window_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (
        !RegisterClassW(&window_class) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS
    ) {
        return false;
    }

    owner_ = owner;

    window_ = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kSettingsClassName,
        L"loqel — Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        kWindowWidth,
        kWindowHeight,
        owner,
        nullptr,
        instance,
        this
    );

    return window_ != nullptr;
}

bool SettingsWindow::show(
    HINSTANCE instance,
    HWND owner,
    const MarkdownCommands& commands,
    const CustomDictionarySettings& dictionary,
    const RecognitionSettings& recognition,
    const CleanupSettings& cleanup,
    const LlmSettings& llm,
    const HotkeySettings& hotkeys
) {
    if (!window_ && !create(instance, owner)) {
        return false;
    }

    populate(commands, dictionary, recognition, cleanup, llm, hotkeys);
    center_on_owner();

    ShowWindow(window_, SW_SHOWNORMAL);
    SetForegroundWindow(window_);
    show_tab(active_tab_);

    return true;
}

void SettingsWindow::populate(
    const MarkdownCommands& commands,
    const CustomDictionarySettings& dictionary,
    const RecognitionSettings& recognition,
    const CleanupSettings& cleanup,
    const LlmSettings& llm,
    const HotkeySettings& hotkeys
) {
    commands_ = commands;
    dictionary_ = dictionary;
    recognition_ = recognition;
    cleanup_ = cleanup;
    llm_ = llm;
    hotkeys_ = hotkeys;

    for (std::size_t i = 0; i < kMarkdownCommandCount; ++i) {
        if (edits_[i]) {
            const std::wstring phrase =
                WindowsText::from_utf8(commands.phrases[i]);
            SetWindowTextW(edits_[i], phrase.c_str());
        }
    }

    std::wstring dictionary_text;

    for (const std::string& phrase : dictionary.phrases) {
        if (!dictionary_text.empty()) {
            dictionary_text += L"\r\n";
        }

        dictionary_text += WindowsText::from_utf8(phrase);
    }

    SetWindowTextW(dictionary_edit_, dictionary_text.c_str());
    SetWindowTextW(
        boost_edit_,
        std::to_wstring(dictionary.boost).c_str()
    );

    std::wstring cleanup_text;
    for (const std::string& word : cleanup.remove_words) {
        if (!cleanup_text.empty()) {
            cleanup_text += L"\r\n";
        }
        cleanup_text += WindowsText::from_utf8(word);
    }
    SetWindowTextW(cleanup_edit_, cleanup_text.c_str());

    SendMessageW(
        mode_combo_,
        CB_SETCURSEL,
        recognition.mode == RecognitionMode::Streaming ? 0 : 1,
        0
    );

    int latency_index = 1;
    if (recognition.latency == LatencyPreset::Low) {
        latency_index = 0;
    }
    else if (recognition.latency == LatencyPreset::HighestAccuracy) {
        latency_index = 2;
    }

    SendMessageW(latency_combo_, CB_SETCURSEL, latency_index, 0);
    SendMessageW(
        llm_checkbox_,
        BM_SETCHECK,
        llm.enabled ? BST_CHECKED : BST_UNCHECKED,
        0
    );
    SendMessageW(
        formatted_hotkey_combo_,
        CB_SETCURSEL,
        hotkeys.formatted_function_key - 1,
        0
    );
    SendMessageW(
        plain_hotkey_combo_,
        CB_SETCURSEL,
        hotkeys.plain_function_key - 1,
        0
    );
}

void SettingsWindow::save_from_controls() {
    MarkdownCommands candidate;

    for (std::size_t i = 0; i < kMarkdownCommandCount; ++i) {
        candidate.phrases[i] = WindowsText::to_utf8(control_text(edits_[i]));

        if (candidate.phrases[i].empty()) {
            MessageBoxW(
                window_,
                L"Command phrases cannot be empty.",
                L"Markdown commands",
                MB_OK | MB_ICONWARNING
            );

            SetFocus(edits_[i]);
            return;
        }

        for (std::size_t previous = 0; previous < i; ++previous) {
            if (
                lowercase(WindowsText::from_utf8(candidate.phrases[i])) ==
                lowercase(WindowsText::from_utf8(candidate.phrases[previous]))
            ) {
                MessageBoxW(
                    window_,
                    L"Each command must use a unique phrase.",
                    L"Markdown commands",
                    MB_OK | MB_ICONWARNING
                );

                SetFocus(edits_[i]);
                return;
            }
        }
    }

    CustomDictionarySettings dictionary;
    dictionary.phrases = line_phrases(dictionary_edit_);

    for (std::size_t i = 0; i < dictionary.phrases.size(); ++i) {
        for (std::size_t previous = 0; previous < i; ++previous) {
            if (
                lowercase(WindowsText::from_utf8(dictionary.phrases[i])) ==
                lowercase(WindowsText::from_utf8(dictionary.phrases[previous]))
            ) {
                MessageBoxW(
                    window_,
                    L"Each custom dictionary phrase must be unique.",
                    L"Custom dictionary",
                    MB_OK | MB_ICONWARNING
                );

                SetFocus(dictionary_edit_);
                return;
            }
        }
    }

    const std::wstring boost_text = control_text(boost_edit_);
    wchar_t* boost_end = nullptr;
    dictionary.boost = std::wcstof(boost_text.c_str(), &boost_end);

    if (
        boost_text.empty() ||
        boost_end == boost_text.c_str() ||
        *boost_end != L'\0' ||
        dictionary.boost < 0.0f ||
        dictionary.boost > 5.0f
    ) {
        MessageBoxW(
            window_,
            L"Dictionary boost must be a number from 0 to 5. Values from 2 to 3 are recommended.",
            L"Custom dictionary",
            MB_OK | MB_ICONWARNING
        );

        SetFocus(boost_edit_);
        return;
    }

    RecognitionSettings recognition;
    recognition.mode =
        SendMessageW(mode_combo_, CB_GETCURSEL, 0, 0) == 1
            ? RecognitionMode::RecordThenTranscribe
            : RecognitionMode::Streaming;

    switch (SendMessageW(latency_combo_, CB_GETCURSEL, 0, 0)) {
        case 0:
            recognition.latency = LatencyPreset::Low;
            break;
        case 2:
            recognition.latency = LatencyPreset::HighestAccuracy;
            break;
        default:
            recognition.latency = LatencyPreset::Balanced;
            break;
    }

    CleanupSettings cleanup;
    cleanup.remove_words = line_phrases(cleanup_edit_);

    for (std::size_t i = 0; i < cleanup.remove_words.size(); ++i) {
        for (std::size_t previous = 0; previous < i; ++previous) {
            if (
                lowercase(WindowsText::from_utf8(cleanup.remove_words[i])) ==
                lowercase(WindowsText::from_utf8(cleanup.remove_words[previous]))
            ) {
                MessageBoxW(
                    window_,
                    L"Each auto-cleanup word or phrase must be unique.",
                    L"Auto cleanup",
                    MB_OK | MB_ICONWARNING
                );

                SetFocus(cleanup_edit_);
                return;
            }
        }
    }

    HotkeySettings hotkeys;
    LlmSettings llm;
    llm.enabled = SendMessageW(
        llm_checkbox_,
        BM_GETCHECK,
        0,
        0
    ) == BST_CHECKED;

    hotkeys.formatted_function_key =
        static_cast<int>(SendMessageW(
            formatted_hotkey_combo_,
            CB_GETCURSEL,
            0,
            0
        )) + 1;
    hotkeys.plain_function_key =
        static_cast<int>(SendMessageW(
            plain_hotkey_combo_,
            CB_GETCURSEL,
            0,
            0
        )) + 1;

    if (
        hotkeys.formatted_function_key == hotkeys.plain_function_key
    ) {
        MessageBoxW(
            window_,
            L"Formatted and plain dictation must use different hotkeys.",
            L"Hotkeys",
            MB_OK | MB_ICONWARNING
        );
        SetFocus(plain_hotkey_combo_);
        return;
    }

    commands_ = std::move(candidate);
    dictionary_ = std::move(dictionary);
    recognition_ = recognition;
    cleanup_ = std::move(cleanup);
    llm_ = llm;
    hotkeys_ = hotkeys;
    ShowWindow(window_, SW_HIDE);
    PostMessageW(owner_, kSavedMessage, 0, 0);
}

const MarkdownCommands& SettingsWindow::commands() const {
    return commands_;
}

const CustomDictionarySettings& SettingsWindow::dictionary() const {
    return dictionary_;
}

const RecognitionSettings& SettingsWindow::recognition() const {
    return recognition_;
}

const CleanupSettings& SettingsWindow::cleanup() const {
    return cleanup_;
}

const LlmSettings& SettingsWindow::llm() const {
    return llm_;
}

const HotkeySettings& SettingsWindow::hotkeys() const {
    return hotkeys_;
}

void SettingsWindow::add_to_tab(
    std::size_t tab,
    HWND control
) {
    if (tab < tab_controls_.size() && control) {
        tab_controls_[tab].push_back(control);
    }
}

void SettingsWindow::show_tab(std::size_t tab) {
    if (tab >= tab_controls_.size()) {
        return;
    }

    active_tab_ = tab;

    for (std::size_t i = 0; i < tab_controls_.size(); ++i) {
        const int visibility = i == tab ? SW_SHOW : SW_HIDE;
        for (HWND control : tab_controls_[i]) {
            ShowWindow(control, visibility);
        }
    }

    for (HWND button : navigation_buttons_) {
        if (button) {
            InvalidateRect(button, nullptr, TRUE);
        }
    }

    for (HWND control : tab_controls_[tab]) {
        if (
            (GetWindowLongPtrW(control, GWL_STYLE) & WS_TABSTOP) != 0 &&
            IsWindowEnabled(control)
        ) {
            SetFocus(control);
            break;
        }
    }
}

void SettingsWindow::center_on_owner() {
    RECT area = {};

    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0)) {
        return;
    }

    const int x =
        area.left +
        ((area.right - area.left) - kWindowWidth) / 2;

    const int y =
        area.top +
        ((area.bottom - area.top) - kWindowHeight) / 2;

    SetWindowPos(
        window_,
        HWND_TOP,
        x,
        y,
        kWindowWidth,
        kWindowHeight,
        SWP_NOACTIVATE
    );
}

void SettingsWindow::destroy() {
    if (window_) {
        DestroyWindow(window_);
        window_ = nullptr;
    }
}

LRESULT CALLBACK SettingsWindow::window_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam
) {
    auto* settings = reinterpret_cast<SettingsWindow*>(
        GetWindowLongPtrW(window, GWLP_USERDATA)
    );

    if (message == WM_NCCREATE) {
        const auto* create_info =
            reinterpret_cast<CREATESTRUCTW*>(lparam);

        settings = static_cast<SettingsWindow*>(
            create_info->lpCreateParams
        );

        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(settings)
        );
    }

    if (!settings) {
        return DefWindowProcW(window, message, wparam, lparam);
    }

    switch (message) {
        case WM_CREATE: {
            settings->ui_font_ = CreateFontW(
                -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"
            );
            settings->heading_font_ = CreateFontW(
                -27, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"
            );
            settings->brand_font_ = CreateFontW(
                -20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"
            );

            HFONT font = settings->ui_font_
                ? settings->ui_font_
                : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

            settings->brand_label_ = CreateWindowExW(
                0,
                L"STATIC",
                L"loqel",
                WS_CHILD | WS_VISIBLE,
                24,
                24,
                160,
                30,
                window,
                nullptr,
                nullptr,
                nullptr
            );
            SendMessageW(
                settings->brand_label_,
                WM_SETFONT,
                reinterpret_cast<WPARAM>(
                    settings->brand_font_ ? settings->brand_font_ : font
                ),
                TRUE
            );

            for (std::size_t i = 0; i < SettingsTabCount; ++i) {
                settings->navigation_buttons_[i] = CreateWindowExW(
                    0,
                    L"BUTTON",
                    kTabNames[i],
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                    12,
                    84 + static_cast<int>(i) * 48,
                    181,
                    40,
                    window,
                    reinterpret_cast<HMENU>(kNavigationBase + i),
                    nullptr,
                    nullptr
                );
                SendMessageW(
                    settings->navigation_buttons_[i],
                    WM_SETFONT,
                    reinterpret_cast<WPARAM>(font),
                    TRUE
                );
            }

            const auto add_page_heading =
                [&](std::size_t tab, const wchar_t* title, const wchar_t* description) {
                    HWND heading = CreateWindowExW(
                        0, L"STATIC", title, WS_CHILD | WS_VISIBLE,
                        250, 36, 590, 38, window, nullptr, nullptr, nullptr
                    );
                    HWND detail = CreateWindowExW(
                        0, L"STATIC", description, WS_CHILD | WS_VISIBLE,
                        250, 80, 590, 42, window, nullptr, nullptr, nullptr
                    );
                    SendMessageW(
                        heading,
                        WM_SETFONT,
                        reinterpret_cast<WPARAM>(
                            settings->heading_font_ ? settings->heading_font_ : font
                        ),
                        TRUE
                    );
                    SendMessageW(detail, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                    settings->add_to_tab(tab, heading);
                    settings->add_to_tab(tab, detail);
                };

            add_page_heading(
                GeneralTab,
                L"General",
                L"Choose the recognition profile used for every dictation session."
            );
            add_page_heading(
                HotkeysTab,
                L"Hotkeys",
                L"Assign separate hold-to-talk keys for formatted and plain text."
            );
            add_page_heading(
                DictionaryTab,
                L"Custom dictionary",
                L"Help recognition prefer names, technical terms, and domain phrases."
            );
            add_page_heading(
                CleanupTab,
                L"Remove words",
                L"One filler word or phrase per line. Applied to formatted dictation."
            );
            add_page_heading(
                MarkdownTab,
                L"Markdown triggers",
                L"Customize the spoken commands converted into Markdown structure."
            );

            HWND intro = CreateWindowExW(
                0,
                L"STATIC",
                L"Spoken command",
                WS_CHILD | WS_VISIBLE,
                450,
                126,
                390,
                22,
                window,
                nullptr,
                nullptr,
                nullptr
            );

            SendMessageW(intro, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            settings->add_to_tab(MarkdownTab, intro);

            for (std::size_t i = 0; i < kMarkdownCommandCount; ++i) {
                const int y = 155 + static_cast<int>(i) * 48;
                const auto command = static_cast<MarkdownCommand>(i);

                const std::wstring label_text = WindowsText::from_utf8(
                    markdown_command_label(command)
                );

                HWND label = CreateWindowExW(
                    0,
                    L"STATIC",
                    label_text.c_str(),
                    WS_CHILD | WS_VISIBLE | SS_LEFT,
                    250,
                    y + 5,
                    180,
                    24,
                    window,
                    nullptr,
                    nullptr,
                    nullptr
                );

                settings->edits_[i] = CreateWindowExW(
                    WS_EX_CLIENTEDGE,
                    L"EDIT",
                    L"",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                    450,
                    y,
                    390,
                    27,
                    window,
                    reinterpret_cast<HMENU>(3000 + i),
                    nullptr,
                    nullptr
                );

                SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                SendMessageW(
                    settings->edits_[i],
                    WM_SETFONT,
                    reinterpret_cast<WPARAM>(font),
                    TRUE
                );

                SendMessageW(settings->edits_[i], EM_SETLIMITTEXT, 200, 0);
                settings->add_to_tab(MarkdownTab, label);
                settings->add_to_tab(MarkdownTab, settings->edits_[i]);
            }

            HWND language_label = CreateWindowExW(
                0,
                L"STATIC",
                L"Language",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                250,
                160,
                180,
                24,
                window,
                nullptr,
                nullptr,
                nullptr
            );

            settings->language_combo_ = CreateWindowExW(
                0,
                L"COMBOBOX",
                L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                450,
                155,
                390,
                120,
                window,
                reinterpret_cast<HMENU>(4200),
                nullptr,
                nullptr
            );
            SendMessageW(
                settings->language_combo_,
                CB_ADDSTRING,
                0,
                reinterpret_cast<LPARAM>(L"English")
            );
            SendMessageW(settings->language_combo_, CB_SETCURSEL, 0, 0);

            HWND model_label = CreateWindowExW(
                0,
                L"STATIC",
                L"Speech model",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                250,
                215,
                180,
                24,
                window,
                nullptr,
                nullptr,
                nullptr
            );

            settings->model_combo_ = CreateWindowExW(
                0,
                L"COMBOBOX",
                L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                450,
                210,
                390,
                120,
                window,
                reinterpret_cast<HMENU>(4201),
                nullptr,
                nullptr
            );
            SendMessageW(
                settings->model_combo_,
                CB_ADDSTRING,
                0,
                reinterpret_cast<LPARAM>(L"Default model")
            );
            SendMessageW(settings->model_combo_, CB_SETCURSEL, 0, 0);

            HWND mode_label = CreateWindowExW(
                0,
                L"STATIC",
                L"Recognition mode",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                250,
                270,
                180,
                24,
                window,
                nullptr,
                nullptr,
                nullptr
            );

            settings->mode_combo_ = CreateWindowExW(
                0,
                L"COMBOBOX",
                L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                    CBS_DROPDOWNLIST | WS_VSCROLL,
                450,
                265,
                390,
                120,
                window,
                reinterpret_cast<HMENU>(4100),
                nullptr,
                nullptr
            );

            SendMessageW(
                settings->mode_combo_,
                CB_ADDSTRING,
                0,
                reinterpret_cast<LPARAM>(L"Live transcription")
            );
            SendMessageW(
                settings->mode_combo_,
                CB_ADDSTRING,
                0,
                reinterpret_cast<LPARAM>(L"Record first, then transcribe")
            );

            HWND latency_label = CreateWindowExW(
                0,
                L"STATIC",
                L"Streaming latency",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                250,
                325,
                180,
                24,
                window,
                nullptr,
                nullptr,
                nullptr
            );

            settings->latency_combo_ = CreateWindowExW(
                0,
                L"COMBOBOX",
                L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                    CBS_DROPDOWNLIST | WS_VSCROLL,
                450,
                320,
                390,
                150,
                window,
                reinterpret_cast<HMENU>(4101),
                nullptr,
                nullptr
            );

            SendMessageW(
                settings->latency_combo_,
                CB_ADDSTRING,
                0,
                reinterpret_cast<LPARAM>(L"Low (~160 ms, less context)")
            );
            SendMessageW(
                settings->latency_combo_,
                CB_ADDSTRING,
                0,
                reinterpret_cast<LPARAM>(L"Balanced (~560 ms)")
            );
            SendMessageW(
                settings->latency_combo_,
                CB_ADDSTRING,
                0,
                reinterpret_cast<LPARAM>(L"Highest accuracy (model context)")
            );

            settings->llm_checkbox_ = CreateWindowExW(
                0,
                L"BUTTON",
                L"Correct final text with the local LLM",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                450,
                380,
                390,
                28,
                window,
                reinterpret_cast<HMENU>(4104),
                nullptr,
                nullptr
            );

            SendMessageW(mode_label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(settings->mode_combo_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(latency_label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(settings->latency_combo_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(settings->llm_checkbox_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(language_label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(settings->language_combo_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(model_label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(settings->model_combo_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            settings->add_to_tab(GeneralTab, language_label);
            settings->add_to_tab(GeneralTab, settings->language_combo_);
            settings->add_to_tab(GeneralTab, model_label);
            settings->add_to_tab(GeneralTab, settings->model_combo_);
            settings->add_to_tab(GeneralTab, mode_label);
            settings->add_to_tab(GeneralTab, settings->mode_combo_);
            settings->add_to_tab(GeneralTab, latency_label);
            settings->add_to_tab(GeneralTab, settings->latency_combo_);
            settings->add_to_tab(GeneralTab, settings->llm_checkbox_);

            HWND formatted_hotkey_label = CreateWindowExW(
                0,
                L"STATIC",
                L"Formatted + cleanup",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                250,
                160,
                180,
                24,
                window,
                nullptr,
                nullptr,
                nullptr
            );

            settings->formatted_hotkey_combo_ = CreateWindowExW(
                0,
                L"COMBOBOX",
                L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                    CBS_DROPDOWNLIST | WS_VSCROLL,
                450,
                155,
                390,
                250,
                window,
                reinterpret_cast<HMENU>(4102),
                nullptr,
                nullptr
            );

            HWND plain_hotkey_label = CreateWindowExW(
                0,
                L"STATIC",
                L"Plain / raw text",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                250,
                215,
                180,
                24,
                window,
                nullptr,
                nullptr,
                nullptr
            );

            settings->plain_hotkey_combo_ = CreateWindowExW(
                0,
                L"COMBOBOX",
                L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                    CBS_DROPDOWNLIST | WS_VSCROLL,
                450,
                210,
                390,
                250,
                window,
                reinterpret_cast<HMENU>(4103),
                nullptr,
                nullptr
            );

            for (int function_key = 1; function_key <= 12; ++function_key) {
                const std::wstring name = L"F" + std::to_wstring(function_key);
                SendMessageW(
                    settings->formatted_hotkey_combo_,
                    CB_ADDSTRING,
                    0,
                    reinterpret_cast<LPARAM>(name.c_str())
                );
                SendMessageW(
                    settings->plain_hotkey_combo_,
                    CB_ADDSTRING,
                    0,
                    reinterpret_cast<LPARAM>(name.c_str())
                );
            }

            HWND hotkey_hint = CreateWindowExW(
                0,
                L"STATIC",
                L"Function keys F1–F12 are available. Each mode must use a different key.",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                450,
                270,
                390,
                42,
                window,
                nullptr,
                nullptr,
                nullptr
            );

            SendMessageW(formatted_hotkey_label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(settings->formatted_hotkey_combo_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(plain_hotkey_label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(settings->plain_hotkey_combo_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(hotkey_hint, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            settings->add_to_tab(HotkeysTab, formatted_hotkey_label);
            settings->add_to_tab(HotkeysTab, settings->formatted_hotkey_combo_);
            settings->add_to_tab(HotkeysTab, plain_hotkey_label);
            settings->add_to_tab(HotkeysTab, settings->plain_hotkey_combo_);
            settings->add_to_tab(HotkeysTab, hotkey_hint);

            HWND dictionary_label = CreateWindowExW(
                0,
                L"STATIC",
                L"Custom dictionary (one word or phrase per line)",
                WS_CHILD | WS_VISIBLE,
                250,
                140,
                590,
                22,
                window,
                nullptr,
                nullptr,
                nullptr
            );

            settings->dictionary_edit_ = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                    ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
                250,
                170,
                590,
                280,
                window,
                reinterpret_cast<HMENU>(4000),
                nullptr,
                nullptr
            );

            HWND boost_label = CreateWindowExW(
                0,
                L"STATIC",
                L"Boost (2–3 recommended, maximum 5)",
                WS_CHILD | WS_VISIBLE,
                250,
                475,
                390,
                22,
                window,
                nullptr,
                nullptr,
                nullptr
            );

            settings->boost_edit_ = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                L"2.0",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                775,
                470,
                65,
                27,
                window,
                reinterpret_cast<HMENU>(4001),
                nullptr,
                nullptr
            );

            HWND cleanup_label = CreateWindowExW(
                0,
                L"STATIC",
                L"Remove words (one word or phrase per line)",
                WS_CHILD | WS_VISIBLE,
                250,
                140,
                590,
                22,
                window,
                nullptr,
                nullptr,
                nullptr
            );

            settings->cleanup_edit_ = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                    ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
                250,
                170,
                590,
                330,
                window,
                reinterpret_cast<HMENU>(4002),
                nullptr,
                nullptr
            );

            SendMessageW(dictionary_label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(settings->dictionary_edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(boost_label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(settings->boost_edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(cleanup_label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(settings->cleanup_edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(settings->dictionary_edit_, EM_SETLIMITTEXT, 32767, 0);
            SendMessageW(settings->boost_edit_, EM_SETLIMITTEXT, 8, 0);
            SendMessageW(settings->cleanup_edit_, EM_SETLIMITTEXT, 32767, 0);
            settings->add_to_tab(DictionaryTab, dictionary_label);
            settings->add_to_tab(DictionaryTab, settings->dictionary_edit_);
            settings->add_to_tab(DictionaryTab, boost_label);
            settings->add_to_tab(DictionaryTab, settings->boost_edit_);
            settings->add_to_tab(CleanupTab, cleanup_label);
            settings->add_to_tab(CleanupTab, settings->cleanup_edit_);

            HWND save = CreateWindowExW(
                0,
                L"BUTTON",
                L"Save",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                680,
                535,
                85,
                30,
                window,
                reinterpret_cast<HMENU>(kSaveButton),
                nullptr,
                nullptr
            );

            HWND cancel = CreateWindowExW(
                0,
                L"BUTTON",
                L"Cancel",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                775,
                535,
                85,
                30,
                window,
                reinterpret_cast<HMENU>(kCancelButton),
                nullptr,
                nullptr
            );

            SendMessageW(save, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            settings->show_tab(GeneralTab);

            return 0;
        }

        case WM_ERASEBKGND: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            RECT client = {};
            GetClientRect(window, &client);

            HBRUSH surface = CreateSolidBrush(kSurfaceColor);
            FillRect(dc, &client, surface);
            DeleteObject(surface);

            RECT sidebar = client;
            sidebar.right = kSidebarWidth;
            HBRUSH navigation = CreateSolidBrush(kSidebarColor);
            FillRect(dc, &sidebar, navigation);
            DeleteObject(navigation);
            return 1;
        }

        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            HWND control = reinterpret_cast<HWND>(lparam);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(
                dc,
                control == settings->brand_label_ ? RGB(245, 247, 250) : kTextColor
            );
            return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
        }

        case WM_DRAWITEM: {
            const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
            if (
                item->CtlID >= kNavigationBase &&
                item->CtlID < kNavigationBase + SettingsTabCount
            ) {
                const std::size_t tab = item->CtlID - kNavigationBase;
                const bool selected = settings->active_tab_ == tab;
                const COLORREF background = selected
                    ? kSidebarSelectedColor
                    : kSidebarColor;

                HBRUSH brush = CreateSolidBrush(background);
                FillRect(item->hDC, &item->rcItem, brush);
                DeleteObject(brush);

                if (selected) {
                    RECT accent = item->rcItem;
                    accent.right = accent.left + 3;
                    HBRUSH accent_brush = CreateSolidBrush(kAccentColor);
                    FillRect(item->hDC, &accent, accent_brush);
                    DeleteObject(accent_brush);
                }

                wchar_t text[64] = {};
                GetWindowTextW(item->hwndItem, text, 64);
                RECT text_area = item->rcItem;
                text_area.left += 18;
                SetBkMode(item->hDC, TRANSPARENT);
                SetTextColor(
                    item->hDC,
                    selected ? RGB(255, 255, 255) : RGB(180, 187, 198)
                );
                HFONT previous = static_cast<HFONT>(SelectObject(
                    item->hDC,
                    settings->ui_font_
                        ? settings->ui_font_
                        : GetStockObject(DEFAULT_GUI_FONT)
                ));
                DrawTextW(
                    item->hDC,
                    text,
                    -1,
                    &text_area,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE
                );
                SelectObject(item->hDC, previous);
                return TRUE;
            }
            break;
        }

        case WM_COMMAND:
            if (
                LOWORD(wparam) >= kNavigationBase &&
                LOWORD(wparam) < kNavigationBase + SettingsTabCount
            ) {
                settings->show_tab(LOWORD(wparam) - kNavigationBase);
                return 0;
            }

            if (LOWORD(wparam) == kSaveButton) {
                settings->save_from_controls();
                return 0;
            }

            if (LOWORD(wparam) == kCancelButton) {
                ShowWindow(window, SW_HIDE);
                return 0;
            }

            break;

        case WM_CLOSE:
            ShowWindow(window, SW_HIDE);
            return 0;

        case WM_DESTROY:
            if (settings->ui_font_) {
                DeleteObject(settings->ui_font_);
                settings->ui_font_ = nullptr;
            }
            if (settings->heading_font_) {
                DeleteObject(settings->heading_font_);
                settings->heading_font_ = nullptr;
            }
            if (settings->brand_font_) {
                DeleteObject(settings->brand_font_);
                settings->brand_font_ = nullptr;
            }
            settings->window_ = nullptr;
            return 0;
    }

    return DefWindowProcW(window, message, wparam, lparam);
}

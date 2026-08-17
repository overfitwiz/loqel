#include "SettingsWindow.h"

#include <algorithm>
#include <cwctype>
#include <sstream>
#include <string>

#include "windows/WindowsText.h"

namespace {

constexpr wchar_t kSettingsClassName[] =
    L"NeMoTalkFormattingSettingsWindow";

constexpr int kSaveButton = 2001;
constexpr int kCancelButton = 2002;
constexpr int kWindowWidth = 900;
constexpr int kWindowHeight = 620;

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

std::vector<std::string> dictionary_phrases(HWND control) {
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
        L"NeMo Talk — Settings",
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
    const RecognitionSettings& recognition
) {
    if (!window_ && !create(instance, owner)) {
        return false;
    }

    populate(commands, dictionary, recognition);
    center_on_owner();

    ShowWindow(window_, SW_SHOWNORMAL);
    SetForegroundWindow(window_);

    if (!edits_.empty() && edits_[0]) {
        SetFocus(edits_[0]);
    }

    return true;
}

void SettingsWindow::populate(
    const MarkdownCommands& commands,
    const CustomDictionarySettings& dictionary,
    const RecognitionSettings& recognition
) {
    commands_ = commands;
    dictionary_ = dictionary;
    recognition_ = recognition;

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
    dictionary.phrases = dictionary_phrases(dictionary_edit_);

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

    commands_ = std::move(candidate);
    dictionary_ = std::move(dictionary);
    recognition_ = recognition;
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
            HFONT font = static_cast<HFONT>(
                GetStockObject(DEFAULT_GUI_FONT)
            );

            HWND intro = CreateWindowExW(
                0,
                L"STATIC",
                L"Customize the spoken triggers converted to Markdown.",
                WS_CHILD | WS_VISIBLE,
                20,
                16,
                850,
                22,
                window,
                nullptr,
                nullptr,
                nullptr
            );

            SendMessageW(intro, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

            for (std::size_t i = 0; i < kMarkdownCommandCount; ++i) {
                const int y = 50 + static_cast<int>(i) * 43;
                const auto command = static_cast<MarkdownCommand>(i);

                const std::wstring label_text = WindowsText::from_utf8(
                    markdown_command_label(command)
                );

                HWND label = CreateWindowExW(
                    0,
                    L"STATIC",
                    label_text.c_str(),
                    WS_CHILD | WS_VISIBLE | SS_LEFT,
                    20,
                    y + 5,
                    155,
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
                    180,
                    y,
                    350,
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
            }

            HWND mode_label = CreateWindowExW(
                0,
                L"STATIC",
                L"Recognition mode",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                20,
                375,
                155,
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
                180,
                370,
                350,
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
                20,
                420,
                155,
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
                180,
                415,
                350,
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

            SendMessageW(mode_label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(settings->mode_combo_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(latency_label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(settings->latency_combo_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

            HWND dictionary_label = CreateWindowExW(
                0,
                L"STATIC",
                L"Custom dictionary (one word or phrase per line)",
                WS_CHILD | WS_VISIBLE,
                555,
                50,
                310,
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
                555,
                76,
                310,
                360,
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
                555,
                452,
                240,
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
                800,
                447,
                65,
                27,
                window,
                reinterpret_cast<HMENU>(4001),
                nullptr,
                nullptr
            );

            SendMessageW(dictionary_label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(settings->dictionary_edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(boost_label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(settings->boost_edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(settings->dictionary_edit_, EM_SETLIMITTEXT, 32767, 0);
            SendMessageW(settings->boost_edit_, EM_SETLIMITTEXT, 8, 0);

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

            return 0;
        }

        case WM_COMMAND:
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
            settings->window_ = nullptr;
            return 0;
    }

    return DefWindowProcW(window, message, wparam, lparam);
}

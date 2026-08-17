#include "WindowsText.h"

#include <windows.h>

namespace WindowsText {

std::wstring from_utf8(std::string_view text) {
    if (text.empty()) {
        return {};
    }

    const int size = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0
    );

    if (size <= 0) {
        return L"[invalid UTF-8]";
    }

    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        size
    );

    return result;
}

std::string to_utf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );

    if (size <= 0) {
        return {};
    }

    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        size,
        nullptr,
        nullptr
    );

    return result;
}

}

#include "TextInjector.h"

#include <windows.h>

#include <vector>

bool TextInjector::insert(
    const std::wstring& text
) const {
    if (text.empty()) {
        return true;
    }

    std::vector<INPUT> inputs;

    inputs.reserve(
        text.size() * 2
    );

    for (wchar_t character : text) {
        INPUT down = {};

        down.type =
            INPUT_KEYBOARD;

        down.ki.wScan =
            character;

        down.ki.dwFlags =
            KEYEVENTF_UNICODE;


        INPUT up = down;

        up.ki.dwFlags =
            KEYEVENTF_UNICODE |
            KEYEVENTF_KEYUP;


        inputs.push_back(down);
        inputs.push_back(up);
    }

    const UINT sent =
        SendInput(
            static_cast<UINT>(
                inputs.size()
            ),
            inputs.data(),
            sizeof(INPUT)
        );

    return sent == inputs.size();
}
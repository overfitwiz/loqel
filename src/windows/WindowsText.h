#pragma once

#include <string>
#include <string_view>

namespace WindowsText {

std::wstring from_utf8(std::string_view text);
std::string to_utf8(std::wstring_view text);

}

#pragma once

#include "libs/nonstd/span.hpp"
#include <vector>
#include <string>

using ByteVector = std::vector<std::byte>;
using ByteView = nonstd::span<const std::byte>;
using StringViewPair = std::pair<std::string_view, std::string_view>;

struct LStringView : std::string_view {
  LStringView(std::string_view s) : std::string_view(s) { }
  LStringView(const char* s) : std::string_view(s) { }
  LStringView(const std::string& s) : std::string_view(s) { }
  LStringView(std::string&& s) = delete;
};

std::string_view unquote(LStringView str);
std::string get_legal_filename(const std::string& filename);
std::string_view as_string_view(ByteView data);
StringViewPair split_content_type(std::string_view content_type);
bool iequals(std::string_view s1, std::string_view s2);

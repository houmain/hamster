#pragma once

#include "libs/nonstd/span.hpp"
#include <vector>
#include <string>

using ByteVector = std::vector<std::byte>;
using ByteView = nonstd::span<const std::byte>;
using StringViewPair = std::pair<std::string_view, std::string_view>;

std::string get_legal_filename(const std::string& filename);
std::string_view as_string_view(ByteView data);
StringViewPair split_content_type(std::string_view content_type);

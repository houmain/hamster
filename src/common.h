#pragma once

#include "libs/nonstd/span.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using ByteVector = std::vector<std::byte>;
using ByteView = nonstd::span<const std::byte>;
using StringViewPair = std::pair<std::string_view, std::string_view>;

struct LStringView : std::string_view {
  LStringView(std::string_view s) : std::string_view(s) { }
  LStringView(const char* s) : std::string_view(s) { }
  LStringView(const std::string& s) : std::string_view(s) { }
  LStringView(std::string&& s) = delete;
};

struct AbsoluteUrl {
  std::string scheme;
  std::string authority;
  std::string path;
};

std::string format_time(time_t time);
time_t parse_time(const std::string& string);
bool is_space(char c);
bool is_punct(char c);
char to_lower(char c);
bool starts_with(std::string_view str, std::string_view with);
std::string_view trim(LStringView str);
std::string_view unquote(LStringView str);
ByteView as_byte_view(std::string_view data);
std::string_view as_string_view(ByteView data);
bool iequals(std::string_view s1, std::string_view s2);
template<typename T, typename... S>
bool iequals_any(T&& a, S&&... b) {
  return (iequals(a, b) || ...);
}

std::filesystem::path utf8_to_path(std::string_view utf8_string);
std::string path_to_utf8(const std::filesystem::path& path);
StringViewPair split_content_type(std::string_view content_type);
std::string convert_charset(std::string data, std::string_view from, std::string_view to);
std::string convert_charset(ByteView data, std::string_view from, std::string_view to);

std::string get_hash(ByteView in);
std::string get_legal_filename(const std::string& filename);
std::string to_local_filename(std::string url, size_t max_length = 255);
std::string url_from_input(std::string_view url_string);
std::optional<AbsoluteUrl> parse_absolute_url(std::string_view url);
std::optional<std::string> resolve_http_url(
  std::string_view base_url, std::string_view reference);

bool is_relative_url(std::string_view url);
std::string_view get_scheme(LStringView url);
std::string_view get_hostname(LStringView url);
std::string_view get_hostname_port(LStringView url);
std::string_view get_scheme_hostname_port_path(LStringView url);
std::string_view get_without_first_domain(LStringView url);
std::string_view get_file_extension(LStringView url);

std::string get_identifying_url(std::string url, ByteView request_data);

std::string generate_id(int length = 8);
std::filesystem::path generate_temporary_filename(std::string prefix);

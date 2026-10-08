
#include "CacheInfo.h"
#include <cstring>

namespace {
  // TODO: could make caching more standard conformant (status code/public...)
  // https://www.w3.org/Protocols/rfc2616/rfc2616-sec13.html
  std::optional<time_t> get_cache_max_age(const Header& reply_header,
      const Header& request_header) {

    const auto find = [](const std::string& string, const char* sequence) -> const char* {
      if (auto pos = std::strstr(string.c_str(), sequence))
        return pos + std::strlen(sequence);
      return nullptr;
    };

    for (const auto& header : { request_header, reply_header }) {
      if (auto it = header.find("Cache-Control"); it != header.end()) {
        if (find(it->second, "no-store"))
          return { };
        if (find(it->second, "no-cache"))
          return 0;
        if (auto value = find(it->second, "s-max-age="))
          return std::atoi(value);
        if (auto value = find(it->second, "max-age="))
          return std::atoi(value);
      }
    }

    if (auto it = reply_header.find("Date"); it != reply_header.end()) {
      const auto date = parse_time(it->second);
      if (auto it = reply_header.find("Expires"); it != reply_header.end())
        return parse_time(it->second) - date;
      if (auto it = reply_header.find("Last-Modified"); it != reply_header.end())
        return (date - parse_time(it->second)) / 10;
    }
    return 0;
  }
} // namespace

std::optional<CacheInfo> get_cache_info(StatusCode status_code,
    const Header& reply_header, const Header& request_header) {

  const auto max_age = get_cache_max_age(reply_header, request_header);
  if (!max_age)
    return { };

  auto age = std::time(nullptr);
  if (auto it = reply_header.find("Date"); it != reply_header.end())
    age -= parse_time(it->second);

  auto cache_info = CacheInfo{ };
  cache_info.expired = (age > max_age.value());

  if (status_code == StatusCode::redirection_moved_permanently)
    cache_info.expired = false;

  if (auto it = reply_header.find("Last-Modified"); it != reply_header.end())
    cache_info.last_modified_time = parse_time(it->second);

  if (auto it = reply_header.find("ETag"); it != reply_header.end())
    cache_info.etag = it->second;

  return cache_info;
}

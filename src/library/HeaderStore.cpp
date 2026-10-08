
#include "HeaderStore.h"
#include <sstream>
#include <cstring>

void HeaderStore::write(std::string url, StatusCode status_code, Header header) {
  m_entries[std::move(url)] = { status_code, std::move(header) };
}

std::string HeaderStore::serialize() const {
  auto ss = std::ostringstream();
  for (const auto& [url, entry] : m_entries) {
    ss << static_cast<int>(entry.status_code) << ' ' << url << '\r' << '\n';
    for (const auto& [key, value] : entry.header)
      ss << '\t' << key << ':' << value << '\r' << '\n';
  }
  return ss.str();
}

void HeaderStore::deserialize(std::string_view data) {
  m_entries.clear();

  auto* header = std::add_pointer_t<Header>{ };
  const auto end = data.end();
  for (auto it = data.begin(); it != end; it += 2) {
    const auto line_begin = it;

    auto colon = end;
    for (; it != end; ++it) {
      if (*it == ':' && colon == end)
        colon = it;

      if (it + 1 < end && it[0] == '\r' && it[1] == '\n')
        break;
    }
    const auto line_end = it;

    if (*line_begin != '\t') {
      auto url_begin = line_begin;
      while (url_begin != line_end && *url_begin != ' ')
        ++url_begin;
      auto& entry = m_entries[{ url_begin + 1, line_end }];
      entry.status_code = static_cast<StatusCode>(std::atoi(&*line_begin));
      header = &entry.header;
    }
    else if (header && colon != end) {
      header->emplace(
        std::string(line_begin + 1, colon),
        std::string(colon + 1, line_end));
    }
  }
}

auto HeaderStore::read(const std::string& url) const -> const Entry* {
  const auto it = m_entries.find(url);
  if (it != m_entries.end())
    return &it->second;
  return nullptr;
}

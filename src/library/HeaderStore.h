#pragma once

#include "common.h"
#include "libs/SimpleWeb/utility.hpp"

using StatusCode = SimpleWeb::StatusCode;
using Header = SimpleWeb::CaseInsensitiveMultimap;

class HeaderStore final {
public:
  struct Entry {
    StatusCode status_code;
    Header header;
  };

  void write(std::string url, StatusCode status_code, Header header);
  std::string serialize() const;

  void deserialize(std::string_view data);
  const Entry* read(const std::string& url) const;

  const std::map<std::string, Entry>& entries() const { return m_entries; }

private:
  std::map<std::string, Entry> m_entries;
};

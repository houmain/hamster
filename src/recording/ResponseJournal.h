#pragma once

#include "proxy/ProxyRequest.h"
#include <chrono>
#include <ctime>
#include <map>
#include <memory>
#include <mutex>
#include <optional>

// Keeps a bounded, short-lived copy of responses which had no recording owner
// when they completed. A late browser observer report can then attach the same
// response to one or more recordings.
class ResponseJournal final {
public:
  struct Entry {
    StatusCode status_code;
    Header header;
    std::shared_ptr<const ByteVector> data;
    time_t response_time;
  };

  explicit ResponseJournal(std::chrono::milliseconds lifetime = std::chrono::seconds(15),
    size_t maximum_bytes = 64 * 1024 * 1024, size_t maximum_entries = 512,
    size_t maximum_response_bytes = 8 * 1024 * 1024);

  void store(std::string url, StatusCode status_code, const Header& header,
    ByteView data, time_t response_time);
  std::optional<Entry> find(std::string url);
  void clear();
  size_t size() const;

private:
  struct TimedEntry {
    Entry response;
    std::chrono::steady_clock::time_point expires;
  };

  static std::string request_url(std::string url);
  void purge_locked(std::chrono::steady_clock::time_point now);
  void evict_oldest_locked();

  const std::chrono::milliseconds m_lifetime;
  const size_t m_maximum_bytes, m_maximum_entries, m_maximum_response_bytes;
  mutable std::mutex m_mutex;
  std::map<std::string, TimedEntry> m_entries;
  size_t m_bytes{};
};

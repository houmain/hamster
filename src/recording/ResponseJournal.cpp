#include "ResponseJournal.h"
#include "common.h"
#include <iterator>
#include <stdexcept>

ResponseJournal::ResponseJournal(std::chrono::milliseconds lifetime,
  size_t maximum_bytes, size_t maximum_entries, size_t maximum_response_bytes)
  : m_lifetime(lifetime), m_maximum_bytes(maximum_bytes),
  m_maximum_entries(maximum_entries), m_maximum_response_bytes(maximum_response_bytes) {}

std::string ResponseJournal::request_url(std::string url) {
  if (const auto fragment = url.find('#'); fragment != std::string::npos)
    url.erase(fragment);
  if (url.empty() || !iequals_any(get_scheme(url), "http", "https"))
    throw std::invalid_argument("Expected an absolute HTTP resource URL");
  return url;
}

void ResponseJournal::purge_locked(std::chrono::steady_clock::time_point now) {
  for (auto it = m_entries.begin(); it != m_entries.end();) {
    if (it->second.expires > now) {
      ++it;
      continue;
    }
    m_bytes -= it->second.response.data->size();
    it = m_entries.erase(it);
  }
}

void ResponseJournal::evict_oldest_locked() {
  if (m_entries.empty())
    return;
  auto oldest = m_entries.begin();
  for (auto it = std::next(m_entries.begin()); it != m_entries.end(); ++it)
    if (it->second.expires < oldest->second.expires)
      oldest = it;
  m_bytes -= oldest->second.response.data->size();
  m_entries.erase(oldest);
}

void ResponseJournal::store(std::string response_url, std::string identifying_url,
  StatusCode status_code, const Header& header, ByteView data, time_t response_time) {
  if (data.size() > m_maximum_response_bytes)
    return;
  response_url = request_url(std::move(response_url));
  identifying_url = request_url(std::move(identifying_url));
  const auto now = std::chrono::steady_clock::now();
  auto copy = std::make_shared<ByteVector>(data.begin(), data.end());
  auto lock = std::lock_guard(m_mutex);
  purge_locked(now);
  if (const auto existing = m_entries.find(identifying_url); existing != m_entries.end()) {
    m_bytes -= existing->second.response.data->size();
    m_entries.erase(existing);
  }
  m_bytes += copy->size();
  auto key = identifying_url;
  m_entries.emplace(std::move(key), TimedEntry{
    Entry{std::move(response_url), std::move(identifying_url), status_code, header,
      std::move(copy), response_time}, now + m_lifetime });
  while (m_entries.size() > m_maximum_entries || m_bytes > m_maximum_bytes)
    evict_oldest_locked();
}

std::vector<ResponseJournal::Entry> ResponseJournal::find(std::string response_url) {
  response_url = request_url(std::move(response_url));
  auto lock = std::lock_guard(m_mutex);
  purge_locked(std::chrono::steady_clock::now());
  auto result = std::vector<Entry>{};
  for (const auto& [_, entry] : m_entries)
    if (entry.response.response_url == response_url)
      result.push_back(entry.response);
  return result;
}

void ResponseJournal::clear() {
  auto lock = std::lock_guard(m_mutex);
  m_entries.clear();
  m_bytes = 0;
}

size_t ResponseJournal::size() const {
  auto lock = std::lock_guard(m_mutex);
  return m_entries.size();
}

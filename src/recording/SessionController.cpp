#include "SessionController.h"
#include <cwctype>
#include <stdexcept>
#include <vector>

namespace {
  bool same_path(const std::filesystem::path& a, const std::filesystem::path& b) {
    if (a.empty() || b.empty())
      return false;
    auto error = std::error_code{};
    if (std::filesystem::equivalent(a, b, error) && !error)
      return true;
#ifdef _WIN32
    auto x = a.native(), y = b.native();
    for (auto& c : x)
      c = static_cast<wchar_t>(std::towlower(c));
    for (auto& c : y)
      c = static_cast<wchar_t>(std::towlower(c));
    return x == y;
#else
    return a == b;
#endif
  }
} // namespace

void SessionController::open(const std::string& id, RecordingSession::Settings settings) {
  if (id.empty())
    throw std::invalid_argument("Empty recording session ID");
  const auto input = resolve_archive_path(settings.input_file);
  const auto output = resolve_archive_path(settings.output_file);
  auto lock = std::lock_guard(m_mutex);
  if (m_stopping)
    throw std::logic_error("Session controller is stopping");
  if (m_sessions.count(id))
    throw std::logic_error("Recording session ID already exists");
  for (const auto& [_, entry] : m_sessions)
    if (same_path(output, entry->output) || same_path(output, entry->input) ||
      same_path(input, entry->output))
      throw std::logic_error("Archive is already in use by a recording session");
  settings.input_file = input;
  settings.output_file = output;
  auto entry = std::make_shared<Entry>(std::move(settings));
  entry->input = input;
  entry->output = output;
  m_sessions.emplace(id, std::move(entry));
}

SessionController::Lease SessionController::acquire(const std::string& id) {
  auto lock = std::lock_guard(m_mutex);
  const auto it = m_sessions.find(id);
  if (it == m_sessions.end() || it->second->closing)
    return {};
  return Lease(it->second, &it->second->session);
}

void SessionController::handle_request(const std::string& id, ProxyRequest request) {
  auto lease = acquire(id);
  if (!lease) {
    request.send_response(StatusCode::server_error_service_unavailable, {}, {});
    return;
  }
  request.hold_lifetime(lease);
  lease->handle_request(std::move(request));
}

bool SessionController::close(const std::string& id) {
  auto entry = std::shared_ptr<Entry>{};
  {
    auto lock = std::lock_guard(m_mutex);
    const auto it = m_sessions.find(id);
    if (it == m_sessions.end())
      return true;
    it->second->closing = true;
    // Only the map may retain the entry before finalization begins.
    if (it->second->finalizing || it->second.use_count() != 1)
      return false;
    it->second->finalizing = true;
    entry = it->second;
  }
  // Never join the archive worker while holding the controller mutex, or
  // from a request/archive callback releasing its last lease.
  auto error = std::exception_ptr{};
  try {
    entry->session.finish();
  }
  catch (...) {
    error = std::current_exception();
  }
  {
    auto lock = std::lock_guard(m_mutex);
    m_sessions.erase(id);
  }
  if (error)
    std::rethrow_exception(error);
  return true;
}

bool SessionController::close_all() {
  auto ids = std::vector<std::string>{};
  {
    auto lock = std::lock_guard(m_mutex);
    m_stopping = true;
    for (const auto& [id, entry] : m_sessions) {
      entry->closing = true;
      ids.push_back(id);
    }
  }
  auto complete = true;
  for (const auto& id : ids)
    if (!close(id))
      complete = false;
  return complete;
}

SessionController::Entry::Entry(RecordingSession::Settings settings)
  : session(std::move(settings)) {}

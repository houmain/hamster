#pragma once

#include "RecordingSession.h"
#include <map>
#include <memory>
#include <mutex>

// Owns sessions until all acquired leases (including asynchronous requests) drain.
// Keep the controller alive and the I/O context running until close_all() succeeds.
class SessionController final {
public:
  using Lease = std::shared_ptr<RecordingSession>;
  void open(const std::string& id, RecordingSession::Settings settings);
  Lease acquire(const std::string& id);
  void handle_request(const std::string& id, ProxyRequest request);
  // Nonblocking while requests are active. Retry from the event loop.
  // Once drained, joins archive writes and commits; propagates finalization errors.
  bool close(const std::string& id);
  bool close_all();

private:
  struct Entry {
    explicit Entry(RecordingSession::Settings settings);

    RecordingSession session;
    std::filesystem::path input;
    std::filesystem::path output;
    bool closing{ };
    bool finalizing{ };
  };

  std::mutex m_mutex;
  std::map<std::string, std::shared_ptr<Entry>> m_sessions;
  bool m_stopping{ };
};

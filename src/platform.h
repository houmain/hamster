#pragma once

#include "common.h"
#include <filesystem>
#include <memory>

enum class Event {
  fatal,
  error,
  info,
  accept,
  loaded,
  resource,
  redirect,
  download_started,
  download_omitted,
  download_finished,
  download_failed,
  download_blocked,
  archive_hit,
  archive_miss,
  forward_failed,
  served,
  writing_failed,
};

std::string read_utf8_textfile(const std::filesystem::path& filename);
std::string get_message_utf8(std::error_code error);
std::string get_message_utf8(const std::exception& ex);

std::unique_ptr<void, void(*)(void*)> begin_output_line(bool to_stderr);
extern bool native_stdio_active;
void write_output(int value);
void write_output(const char* utf8);
void write_output(const std::string& utf8);
void write_output(Event event);
template <typename T>
void write_output(T value) { write_output(static_cast<int>(value)); }

template <typename... Args>
void log(Event event, Args&&... args) {
  auto guard = begin_output_line(native_stdio_active || event == Event::fatal);
  write_output(event);
  write_output(" ");
  (write_output(args), ...);
}


extern int run(int argc, const char* argv[]) noexcept;

// Serializes proxy ownership for a configured listener port. Port zero is
// intentionally unlocked so isolated/test instances can request ephemeral ports.
class ProxyInstanceLock final {
public:
  explicit ProxyInstanceLock(int port);
  ~ProxyInstanceLock();
  ProxyInstanceLock(const ProxyInstanceLock&) = delete;
  ProxyInstanceLock& operator=(const ProxyInstanceLock&) = delete;

  bool try_lock();
  void unlock();

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

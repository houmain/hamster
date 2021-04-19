#pragma once

#include "libs/TinyProcessLib/process.hpp"
#include <mutex>
#include <condition_variable>
#include <functional>
#include <filesystem>

class Webrecorder {
public:
  Webrecorder(const std::vector<std::string>& arguments,
              const std::string& working_directory);
  ~Webrecorder();

  void stop();
  bool finished() const;
  void for_each_output_line(const std::function<void(std::string_view)>& callback);

private:
  void thread_func() noexcept;
  void handle_output(const char* data, size_t size);
  void handle_finished();

  std::optional<TinyProcessLib::Process> m_process;
  std::thread m_thread;
  mutable std::mutex m_output_mutex;
  std::condition_variable m_output_signal;
  std::vector<char> m_output_buffer;
  bool m_finished{ };
};

#pragma once

#include "libs/TinyProcessLib/process.hpp"
#include <mutex>
#include <condition_variable>
#include <functional>
#include <filesystem>

class Webrecorder {
public:
  Webrecorder(std::filesystem::path filename,
              const std::vector<std::string>& arguments,
              const std::string& working_directory,
              std::function<void(std::filesystem::path)> on_finished);
  ~Webrecorder();

  void stop();
  bool finished() const;
  void for_each_output_line(const std::function<void(std::string)>& callback);

private:
  void thread_func() noexcept;
  void handle_output(const char* data, size_t size);
  void handle_finished();

  const std::filesystem::path m_filename;
  const std::function<void(const std::filesystem::path&)> m_on_finished;
  TinyProcessLib::Process m_process;
  std::thread m_thread;
  mutable std::mutex m_output_mutex;
  std::condition_variable m_output_signal;
  std::vector<char> m_output_buffer;
  bool m_finished{ };
};

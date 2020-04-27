
#include "Webrecorder.h"
#include <cstring>

#if defined(_WIN32)
# define WIN32_LEAN_AND_MEAN
# if !defined(NOMINMAX)
#   define NOMINMAX
# endif
# include <windows.h>
#endif

using namespace std::placeholders;

namespace {
#if defined(_WIN32)
  std::wstring utf8_to_native(const std::string& str) {
    auto result = std::wstring();
    result.resize(::MultiByteToWideChar(CP_UTF8, 0,
      str.data(), static_cast<int>(str.size()),
      NULL, 0));
    ::MultiByteToWideChar(CP_UTF8, 0,
      str.data(), static_cast<int>(str.size()),
      result.data(), static_cast<int>(result.size()));
    return result;
  }
  std::vector<std::wstring> utf8_to_native(const std::vector<std::string>& args) {
    auto result = std::vector<std::wstring>();
    for (auto& arg : args)
      result.push_back(utf8_to_native(arg));
    return result;
  }
#else
  const std::string& utf8_to_native(const std::string& arg) { return arg; }
  const std::vector<std::string>& utf8_to_native(const std::vector<std::string>& args) {
    return args;
  }
#endif
} // namespace

Webrecorder::Webrecorder(
    const std::vector<std::string>& arguments,
    const std::string& working_directory)
  : m_process(utf8_to_native(arguments), utf8_to_native(working_directory),
      std::bind(&Webrecorder::handle_output, this, _1, _2)) {

  if (!m_process.get_id())
    throw std::runtime_error("starting webrecorder process failed");

  m_thread = std::thread(&Webrecorder::thread_func, this);

  // wait for first output, so first poll receives it, to optimize latency
  auto lock = std::unique_lock(m_output_mutex);
  m_output_signal.wait_for(lock, std::chrono::milliseconds(500),
    [&]() { return !m_output_buffer.empty(); });
}

Webrecorder::~Webrecorder() {
  stop();
  m_thread.join();
}

void Webrecorder::stop() {
#if defined(_WIN32)
  // almost too easy
  TinyProcessLib::Process ctrl_c_process(
    utf8_to_native("ctrl_c " + std::to_string(m_process.get_id())));
  if (ctrl_c_process.get_exit_status())
    m_process.kill();
#else
  m_process.kill();
#endif
}

void Webrecorder::for_each_output_line(
    const std::function<void(std::string)>& callback) {

  auto lock = std::lock_guard(m_output_mutex);
  const auto begin = m_output_buffer.begin();
  const auto end = m_output_buffer.end();
  auto line_begin = begin;
  for (;;) {
    const auto it = std::find(line_begin, end, '\n');
    if (it == end)
      break;
    callback(std::string(line_begin, it));
    line_begin = it + 1;
  }
  m_output_buffer.erase(begin, line_begin);
}

bool Webrecorder::finished() const {
  auto lock = std::lock_guard(m_output_mutex);
  return m_finished;
}

void Webrecorder::thread_func() noexcept {
  m_process.get_exit_status();
  handle_finished();
}

void Webrecorder::handle_output(const char* data, size_t size) {
  auto lock = std::unique_lock(m_output_mutex);
  m_output_buffer.insert(end(m_output_buffer), data, data + size);
  lock.unlock();
  m_output_signal.notify_one();
}

void Webrecorder::handle_finished() {
  auto lock = std::unique_lock(m_output_mutex);
  const auto message = std::string_view("FINISHED\n");
  m_output_buffer.insert(end(m_output_buffer), message.begin(), message.end());
  m_finished = true;
  lock.unlock();
  m_output_signal.notify_one();
}

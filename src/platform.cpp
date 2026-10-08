
#include "platform.h"
#include <fstream>
#include <sstream>
#include <mutex>
#include <stdexcept>

bool native_stdio_active = false;

void write_output(Event event) {
  write_output([&]() {
    switch (event) {
      case Event::fatal: return "FATAL";
      case Event::error: return "ERROR";
      case Event::info: return "INFO";
      case Event::accept: return "ACCEPT";
      case Event::loaded: return "PAGE_LOADED";
      case Event::resource: return "PAGE_RESOURCE";
      case Event::redirect: return "REDIRECT";
      case Event::download_started: return "DOWNLOADING";
      case Event::download_omitted: return "DOWNLOAD_OMITTED";
      case Event::download_finished: return "DOWNLOAD_FINISHED";
      case Event::download_failed: return "DOWNLOAD_FAILED";
      case Event::download_blocked: return "DOWNLOAD_BLOCKED";
      case Event::archive_hit: return "ARCHIVE_HIT";
      case Event::archive_miss: return "ARCHIVE_MISS";
      case Event::forward_failed: return "FORWARD_FAILED";
      case Event::served: return "SERVED";
      case Event::writing_failed: return "WRITING_FAILED";
    }
    return "";
  }());
}

std::string read_utf8_textfile(const std::filesystem::path& filename) {
  auto file = std::ifstream(filename);
  if (!file.good())
    throw std::runtime_error("reading '" + path_to_utf8(filename) + "' failed");
  auto buffer = std::stringstream();
  buffer << file.rdbuf();
  return { buffer.str() };
}

#if !defined(_WIN32)

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace {
  std::mutex g_output_mutex;
  FILE* g_current_output_file;

  void end_output_line(void*) {
    std::fputc('\n', g_current_output_file);
    std::fflush(g_current_output_file);
    g_current_output_file = nullptr;
    g_output_mutex.unlock();
  }
} // namespace

std::string get_message_utf8(std::error_code error) {
  // End of file
  if (error.value() == 2)
    return { };
  return error.message();
}

std::string get_message_utf8(const std::exception& ex) {
  return ex.what();
}

std::unique_ptr<void, void(*)(void*)> begin_output_line(bool to_stderr) {
  g_output_mutex.lock();
  g_current_output_file = (to_stderr ? stderr : stdout);
  return { reinterpret_cast<void*>(1), &end_output_line };
}

void write_output(int value) {
  std::fprintf(g_current_output_file, "%i", value);
}

void write_output(const char* utf8) {
  for (auto c = utf8; *c; ++c) {
    if (*c == '\n') {
      std::fputc('\\', g_current_output_file);
      std::fputc('n', g_current_output_file);
    }
    else if (*c == '\r') {
      std::fputc('\\', g_current_output_file);
      std::fputc('r', g_current_output_file);
    }
    else {
      std::fputc(*c, g_current_output_file);
    }
  }
}

void write_output(const std::string& utf8) {
  write_output(utf8.c_str());
}

struct ProxyInstanceLock::Impl {
  explicit Impl(int port) {
    if (port == 0)
      return;
    const auto filename = std::filesystem::temp_directory_path() /
      ("bookmark-hamster-proxy-" + std::to_string(static_cast<long long>(getuid())) +
       "-" + std::to_string(port) + ".lock");
    descriptor = ::open(filename.c_str(), O_CREAT | O_RDWR, 0600);
    if (descriptor == -1)
      throw std::runtime_error("Creating proxy instance lock failed: " +
        std::string(std::strerror(errno)));
  }

  ~Impl() {
    if (descriptor != -1)
      ::close(descriptor);
  }

  int descriptor{-1};
  bool locked{};
};

ProxyInstanceLock::ProxyInstanceLock(int port) : m_impl(std::make_unique<Impl>(port)) {}
ProxyInstanceLock::~ProxyInstanceLock() { unlock(); }

bool ProxyInstanceLock::try_lock() {
  if (m_impl->locked || m_impl->descriptor == -1)
    return true;
  if (::flock(m_impl->descriptor, LOCK_EX | LOCK_NB) == 0) {
    m_impl->locked = true;
    return true;
  }
  if (errno == EWOULDBLOCK)
    return false;
  throw std::runtime_error("Acquiring proxy instance lock failed: " +
    std::string(std::strerror(errno)));
}

void ProxyInstanceLock::unlock() {
  if (!m_impl || !m_impl->locked)
    return;
  (void)::flock(m_impl->descriptor, LOCK_UN);
  m_impl->locked = false;
}

int main(int argc, const char* argv[]) {
  return run(argc, argv);
}

#else // _WIN32
# if !defined(WIN32_LEAN_AND_MEAN)
# define WIN32_LEAN_AND_MEAN
# endif
# if !defined(NOMINMAX)
#   define NOMINMAX
# endif
# include <windows.h>
# include <shellapi.h>
# include <csignal>

namespace {
  std::mutex g_output_mutex;
  FILE* g_current_output_file;

  void end_output_line(void*) {
    std::fputwc(L'\n', g_current_output_file);
    std::fflush(g_current_output_file);
    g_current_output_file = nullptr;
    g_output_mutex.unlock();
  }

  void write_output_wide(std::wstring_view str) {
    for (auto c : str) {
      if (c == L'\n') {
        std::fputwc(L'\\', g_current_output_file);
        std::fputwc(L'n', g_current_output_file);
      }
      else if (c == L'\r') {
        std::fputwc(L'\\', g_current_output_file);
        std::fputwc(L'r', g_current_output_file);
      }
      else {
        std::fputwc(c, g_current_output_file);
      }
    }
  }

  std::wstring multibyte_to_wide(std::string_view str) {
    auto result = std::wstring();
    result.resize(MultiByteToWideChar(CP_ACP, 0, 
      str.data(), static_cast<int>(str.size()),
      NULL, 0));
    MultiByteToWideChar(CP_ACP, 0, 
      str.data(), static_cast<int>(str.size()), 
      result.data(), static_cast<int>(result.size()));
    return result;
  }

  std::string wide_to_utf8(std::wstring_view str) {
    auto result = std::string();
    result.resize(WideCharToMultiByte(CP_UTF8, 0, str.data(), 
      static_cast<int>(str.size()), NULL, 0, NULL, 0));
    WideCharToMultiByte(CP_UTF8, 0, 
      str.data(), static_cast<int>(str.size()),
      result.data(), static_cast<int>(result.size()),
      NULL, 0);
    return result;
  }

  std::wstring utf8_to_wide(std::string_view str) {
    auto result = std::wstring();
    result.resize(MultiByteToWideChar(CP_UTF8, 0,
      str.data(), static_cast<int>(str.size()),
      NULL, 0));
    MultiByteToWideChar(CP_UTF8, 0,
      str.data(), static_cast<int>(str.size()),
      result.data(), static_cast<int>(result.size()));
    return result;
  }
} // namespace

std::string get_message_utf8(std::error_code error) {
  // End of file
  if (error.value() == 2)
    return { };
  return wide_to_utf8(multibyte_to_wide(error.message()));
}

std::string get_message_utf8(const std::exception& ex) {
  return wide_to_utf8(multibyte_to_wide(ex.what()));
}

std::unique_ptr<void, void(*)(void*)> begin_output_line(bool to_stderr) {
  g_output_mutex.lock();
  g_current_output_file = (to_stderr ? stderr : stdout);
  return { reinterpret_cast<void*>(1), &end_output_line };
}

void write_output(int value) {
  std::fprintf(g_current_output_file, "%i", value);
}

void write_output(const char* utf8) {
  write_output_wide(utf8_to_wide(utf8));
}

void write_output(const std::string& utf8) {
  write_output_wide(utf8_to_wide(utf8));
}

struct ProxyInstanceLock::Impl {
  explicit Impl(int port) {
    if (port == 0)
      return;
    const auto name = L"Local\\BookmarkHamsterProxy-" + std::to_wstring(port);
    handle = CreateMutexW(nullptr, FALSE, name.c_str());
    if (!handle)
      throw std::runtime_error("Creating proxy instance mutex failed");
  }

  ~Impl() {
    if (handle)
      CloseHandle(handle);
  }

  HANDLE handle{};
  bool locked{};
};

ProxyInstanceLock::ProxyInstanceLock(int port) : m_impl(std::make_unique<Impl>(port)) {}
ProxyInstanceLock::~ProxyInstanceLock() { unlock(); }

bool ProxyInstanceLock::try_lock() {
  if (m_impl->locked || !m_impl->handle)
    return true;
  const auto result = WaitForSingleObject(m_impl->handle, 0);
  if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED) {
    m_impl->locked = true;
    return true;
  }
  if (result == WAIT_TIMEOUT)
    return false;
  throw std::runtime_error("Acquiring proxy instance mutex failed");
}

void ProxyInstanceLock::unlock() {
  if (!m_impl || !m_impl->locked)
    return;
  ReleaseMutex(m_impl->handle);
  m_impl->locked = false;
}

int wmain(int argc, wchar_t* wargv[]) {
  auto argv_strings = std::vector<std::string>();
  for (auto i = 0; i < argc; ++i)
    argv_strings.push_back(wide_to_utf8(wargv[i]));
  auto argv = std::vector<const char*>();
  for (const auto& string : argv_strings)
    argv.push_back(string.c_str());

  SetConsoleCtrlHandler([](DWORD) {
    std::raise(SIGINT);
    // wait until it properly shut down
    for (;; Sleep(100));
    return TRUE;
  }, TRUE);

  return run(argc, argv.data());
}

#endif // _WIN32

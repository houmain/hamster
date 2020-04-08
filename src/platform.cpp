
#include "platform.h"
#include <cstring>
#include <csignal>
#include <array>

namespace {
  const auto default_library_root_name = "BookmarkHamster";
  std::filesystem::path g_webrecorder_path;
  std::filesystem::path g_default_library_root;
} // namespace

const std::filesystem::path& webrecorder_path() {
  return g_webrecorder_path;
}

const std::filesystem::path& default_library_root() {
  return g_default_library_root;
}

#if !defined(_WIN32)
#  include <unistd.h>

int main(int argc, const char* argv[], const char* env[]) {
  auto path = std::array<char, 1024>{ };
  readlink("/proc/self/exe", path.data(), path.size());
  g_webrecorder_path =
    std::filesystem::path(path.data()).replace_filename("webrecorder");

  for (auto it = env; *it; ++it)
    if (!std::strncmp(*it, "HOME=", 5)) {
      g_default_library_root = std::filesystem::u8path(*it + 5) / default_library_root_name;
      break;
    }

  // make SIGINT interrupt without restarting
  struct sigaction a{ };
  a.sa_handler = [](int) { };
  a.sa_flags = 0;
  sigemptyset(&a.sa_mask);
  sigaction(SIGINT, &a, nullptr);

  return run(argc, argv);
}

#else // _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <Windows.h>
#  include <Shlobj.h>
#  include <io.h>
#  include <fcntl.h>

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

int wmain(int argc, wchar_t* wargv[]) {
  auto path = std::array<wchar_t, MAX_PATH>{ };
  GetModuleFileNameW(NULL, path.data(), path.size());
  g_webrecorder_path =
    std::filesystem::path(path.data()).replace_filename("webrecorder.exe");

  SHGetFolderPathW(NULL, CSIDL_MYDOCUMENTS, NULL, SHGFP_TYPE_CURRENT, path.data());
  g_default_library_root = std::filesystem::path(path.data()) / default_library_root_name;

  (void)_setmode(fileno(stdout), _O_BINARY);
  (void)_setmode(fileno(stdin), _O_BINARY);

  auto argv_strings = std::vector<std::string>();
  for (auto i = 0; i < argc; ++i)
    argv_strings.push_back(wide_to_utf8(wargv[i]));
  auto argv_vector = std::vector<const char*>();
  for (const auto& string : argv_strings)
    argv_vector.push_back(string.c_str());
  const auto argv = argv_vector.data();

  return run(argc, argv);
}
#endif // _WIN32

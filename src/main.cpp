
#include "StdioInterface.h"
#include <cstring>
#include <array>

#if !defined(_WIN32)
#  include <unistd.h>

int main(int argc, const char* argv[], const char* env[]) try {
  auto settings = Settings{ };

  auto path = std::array<char, 1024>{ };
  readlink("/proc/self/exe", path.data(), path.size());
  settings.webrecorder_path =
    std::filesystem::path(path.data()).replace_filename("webrecorder");

  for (auto it = env; *it; ++it)
    if (!std::strncmp(*it, "HOME=", 5)) {
      settings.default_library_root = std::filesystem::u8path(*it + 5) / "PagesOwned";
      break;
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

int wmain(int argc, wchar_t* wargv[]) try {
  auto settings = Settings{ };

  auto path = std::array<wchar_t, MAX_PATH>{ };
  GetModuleFileNameW(NULL, path.data(), path.size());
  settings.webrecorder_path =
    std::filesystem::path(path.data()).replace_filename("webrecorder.exe");

  SHGetFolderPathW(NULL, CSIDL_MYDOCUMENTS, NULL, SHGFP_TYPE_CURRENT, path.data());
  settings.default_library_root = std::filesystem::path(path.data()) / "PagesOwned";

  (void)_setmode(fileno(stdout), _O_BINARY);
  (void)_setmode(fileno(stdin), _O_BINARY);

  auto argv_strings = std::vector<std::string>();
  for (auto i = 0; i < argc; ++i)
    argv_strings.push_back(wide_to_utf8(wargv[i]));
  auto argv_vector = std::vector<const char*>();
  for (const auto& string : argv_strings)
    argv_vector.push_back(string.c_str());
  const auto argv = argv_vector.data();
#endif // _WIN32

  if (!interpret_commandline(settings, argc, argv)) {
    print_help_message(argv[0]);
    return 1;
  }

  auto logic = Logic(settings);

  for (const auto& json : settings.json_input) {
    auto request = json::parse(json);
    auto response = json::build_string([&](Response& response) {
      response.StartObject();
      logic.handle_request(response, request);
      response.EndObject();
    });
    std::fprintf(stdout, "%s\n", response.c_str());
  }

  if (settings.run_stdio_interface) {
    auto stdio = StdioInterface(&logic);
    stdio.run();
  }
}
catch (const std::exception& ex) {
  std::fprintf(stderr, "unhanded exception: %s\n", ex.what());
}

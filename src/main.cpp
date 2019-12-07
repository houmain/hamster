
#include "Json.h"
#include "Webrecorder.h"
#include "common.h"
#include <cstdio>
#include <map>
#include <filesystem>
#include <array>

namespace {
  using Response = json::Writer;
  using Request = json::Document;

  std::filesystem::path g_default_library_root;
  std::filesystem::path g_webrecorder_path;
  std::filesystem::path g_library_root;
  std::map<int, Webrecorder> g_webrecorders;

  std::filesystem::path to_full_path(const std::vector<std::string_view> strings) {
    if (g_library_root.empty())
      throw std::runtime_error("library root not set");
    auto path = std::filesystem::path();
    for (const auto& s : strings)
      path /= std::filesystem::u8path(get_legal_filename(std::string(s)));
    return g_library_root / path.lexically_normal();
  }

  void do_move_file(const std::filesystem::path& from, const std::filesystem::path& to) {
    if (from == to)
      return;

    if (std::filesystem::is_directory(from) && std::filesystem::is_directory(to)) {
      // merge directories
      for (const auto& file : std::filesystem::directory_iterator(from))
        do_move_file(file.path(), to / relative(file.path(), from));
    }
    else {
      if (std::filesystem::exists(to))
        throw std::runtime_error("file exists");

      std::filesystem::create_directories(to.parent_path());
      std::filesystem::rename(from, to);
    }
  }

  void move_file(Response&, const Request& request) {
    const auto from_path = to_full_path(json::get_string_list(request, "from"));
    const auto to_path = to_full_path(json::get_string_list(request, "to"));
    if (std::filesystem::exists(from_path))
      do_move_file(from_path, to_path);
  }

  void delete_file(Response&, const Request& request) {
    auto path = json::get_string_list(request, "path");
    const auto file_path = to_full_path(path);
    const auto undelete_id = json::try_get_string(request, "undeleteId");
    if (undelete_id) {
      path.insert(begin(path), { ".trash", *undelete_id });
      const auto trash_path = to_full_path(path);
      if (std::filesystem::exists(file_path))
        do_move_file(file_path, trash_path);
    }
    else {
      if (std::filesystem::is_regular_file(file_path))
        std::filesystem::remove(file_path);
      else if (std::filesystem::is_directory(file_path))
        std::filesystem::remove_all(file_path);
    }
  }

  void undelete_file(Response&, const Request& request) {
    const auto undelete_id = json::get_string(request, "undeleteId");
    const auto trash_path = to_full_path({ ".trash", undelete_id });
    if (!std::filesystem::is_directory(trash_path))
      return;
    for (const auto& file : std::filesystem::directory_iterator(trash_path))
      do_move_file(file.path(), g_library_root / relative(file.path(), trash_path));
    std::filesystem::remove_all(trash_path);
  }

  void start_recording(Response&, const Request& request) {
    const auto id = json::get_int(request, "id");
    const auto url = json::get_string(request, "url");
    const auto filename = json::get_string(request, "filename");
    const auto path = to_full_path(json::get_string_list(request, "path"));
    std::filesystem::create_directories(path);

    auto arguments = std::vector<std::string>{
      g_webrecorder_path.u8string(),
      "-d", "B",
      "-f", "P",
      "-v", "R",
      "-i", '\"' + std::string(url) + '\"',
      "-o", '\"' + std::string(filename) + '\"'
    };
    g_webrecorders.emplace(std::piecewise_construct,
      std::forward_as_tuple(id),
      std::forward_as_tuple(std::move(arguments), path.u8string()));
  }

  void stop_recording(Response&, const Request& request) {
    const auto id = json::get_int(request, "id");
    if (auto it = g_webrecorders.find(id); it != g_webrecorders.end())
      it->second.stop();
  }

  void get_recording_output(Response& response, const Request& request) {
    const auto id = json::get_int(request, "id");
    if (auto it = g_webrecorders.find(id); it != g_webrecorders.end()) {
      // cleanup stopped recorder
      if (it->second.finished()) {
        g_webrecorders.erase(it);
        return;
      }
      response.Key("events");
      response.StartArray();
      it->second.for_each_output_line([&](const auto& line) { response.String(line); });
      response.EndArray();
    }
  }

  void set_library_root(Response& response, const Request& request) {
    const auto path = json::try_get_string(request, "path");
    auto library_root = std::filesystem::u8path(path.value_or("")).lexically_normal();

    // reset to default
    auto error = std::error_code();
    if (library_root.empty() ||
        !std::filesystem::exists(library_root, error)) {
      library_root = g_default_library_root;
      std::filesystem::create_directories(library_root);
    }
    // succeeded
    g_library_root = library_root;

    response.Key("path");
    response.String(library_root.u8string());
  }

  void handle_request(Response& response, const Request& request) {
    using Handler = std::function<void(Response&, const Request&)>;
    static const auto s_action_handlers = std::map<std::string_view, Handler> {
      { "moveFile", &move_file },
      { "deleteFile", &delete_file },
      { "undeleteFile", &undelete_file },
      { "startRecording", &start_recording },
      { "stopRecording", &stop_recording },
      { "getRecordingOutput", &get_recording_output },
      { "setLibraryRoot", &set_library_root },
    };

    response.StartObject();
    response.Key("requestId");
    response.Int(json::get_int(request, "requestId"));
    const auto action = json::get_string(request, "action");
    const auto it = s_action_handlers.find(action);
    if (it == s_action_handlers.end())
      throw std::runtime_error("invalid action " + std::string(action));
    it->second(response, request);
    response.EndObject();
  }

  void handle_error(Response& response,
      const json::Document& request, const std::exception& ex) {
    response.StartObject();
    response.Key("requestId");
    response.Int(json::get_int(request, "requestId"));
    response.Key("error");
    response.String(ex.what());
    response.EndObject();
  }

  bool read(std::vector<char>& buffer) {
    auto length = uint32_t{ };
    if (std::fread(&length, 1, 4, stdin) == 4) {
      buffer.resize(length);
      if (std::fread(buffer.data(), 1, length, stdin) == length)
        return true;
    }
    return false;
  }

  void write(std::string_view message) {
    auto length = static_cast<uint32_t>(message.size());
    if (std::fwrite(&length, 1, 4, stdout) == 4) {
      std::fwrite(message.data(), 1, length, stdout);
      std::fflush(stdout);
    }
  }
} // namespace

#if !defined(_WIN32)
#  include <unistd.h>

int main(int, const char*[], const char* env[]) {
  auto path = std::array<char, 1024>{ };
  readlink("/proc/self/exe", path.data(), path.size());
  g_webrecorder_path = std::filesystem::path(path.data()).replace_filename("webrecorder");

  for (auto it = env; *it; ++it)
    if (!std::strncmp(*it, "HOME=", 5)) {
      g_default_library_root = std::filesystem::u8path(*it + 5) / "PagesOwned";
      break;
    }

#else // _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <Windows.h>
#  include <Shlobj.h>
#  include <io.h>
#  include <fcntl.h>

int wmain(int, wchar_t* argv[]) {
  auto path = std::array<wchar_t, MAX_PATH>{ };
  GetModuleFileNameW(NULL, path.data(), path.size());
  g_webrecorder_path = std::filesystem::path(path.data()).replace_filename("webrecorder.exe");

  SHGetFolderPathW(NULL, CSIDL_MYDOCUMENTS, NULL, SHGFP_TYPE_CURRENT, path.data());
  g_default_library_root = std::filesystem::path(path.data()) / "PagesOwned";

  (void)_setmode(fileno(stdout), _O_BINARY);
  (void)_setmode(fileno(stdin), _O_BINARY);
#endif // _WIN32

  if (!std::filesystem::exists(g_webrecorder_path)) {
    std::fprintf(stderr, "webrecorder not found\n");
    return 1;
  }

  auto buffer = std::vector<char>();
  try {
    while (read(buffer)) {
      auto request = json::parse(std::string_view(buffer.data(), buffer.size()));
      try {
        write(json::build_string([&](Response& response) {
          handle_request(response, request);
        }));
      }
      catch (const std::exception& ex) {
        write(json::build_string([&](Response& response) {
          handle_error(response, request, ex);
        }));
      }
    }
  }
  catch (const std::exception& ex) {
    std::fprintf(stderr, "unhanded exception: %s\n", ex.what());
  }
}

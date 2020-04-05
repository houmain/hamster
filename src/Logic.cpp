
#include "Logic.h"
#include "Database.h"
#include "common.h"
#include "platform.h"
#include <random>
#include <fstream>
#define NOC_FILE_DIALOG_IMPLEMENTATION
#include "libs/noc/noc_file_dialog.h"

namespace {
  const auto trash_directory_name = ".trash";
  const auto index_database_filename = ".pagesowned.sqlite";

  std::filesystem::path generate_temporary_filename() {
    auto rand = std::random_device();
    auto filename = std::string("pagesowned_");
    for (auto i = 0; i < 10; i++)
      filename.push_back('0' + rand() % 10);
    filename += ".tmp";
    return std::filesystem::temp_directory_path() / filename;
  }

  void create_directories_handle_symlinks(const std::filesystem::path& path) {
    if (!std::filesystem::is_symlink(path))
      std::filesystem::create_directories(path);
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

      create_directories_handle_symlinks(to.parent_path());
      std::filesystem::rename(from, to);
    }
  }
} // namespace

Logic::Logic(const Settings& settings)
  : m_settings(settings) {
}

Logic::~Logic() {
  if (!m_block_hosts_file.empty())
    std::filesystem::remove(m_block_hosts_file);
  if (!m_bypass_hosts_file.empty())
    std::filesystem::remove(m_bypass_hosts_file);
}

std::filesystem::path Logic::to_full_path(
    const std::vector<std::string_view>& strings) const {
  if (m_library_root.empty())
    throw std::runtime_error("library root not set");
  auto path = std::filesystem::path();
  for (const auto& s : strings)
    path /= std::filesystem::u8path(get_legal_filename(std::string(s)));
  return m_library_root / path.lexically_normal();
}

void Logic::move_file(Response&, const Request& request) {
  const auto from_path = to_full_path(json::get_string_list(request, "from"));
  const auto to_path = to_full_path(json::get_string_list(request, "to"));
  if (std::filesystem::exists(from_path))
    do_move_file(from_path, to_path);
}

void Logic::delete_file(Response&, const Request& request) {
  auto path = json::get_string_list(request, "path");
  const auto file_path = to_full_path(path);
  const auto undelete_id = json::try_get_string(request, "undeleteId");
  if (undelete_id) {
    path.insert(begin(path), { trash_directory_name, *undelete_id });
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

void Logic::undelete_file(Response&, const Request& request) {
  const auto undelete_id = json::get_string(request, "undeleteId");
  const auto trash_path = to_full_path({ trash_directory_name, undelete_id });
  if (!std::filesystem::is_directory(trash_path))
    return;
  for (const auto& file : std::filesystem::directory_iterator(trash_path))
    do_move_file(file.path(), m_library_root / relative(file.path(), trash_path));
  std::filesystem::remove_all(trash_path);
}

void Logic::start_recording(Response&, const Request& request) {
  const auto id = json::get_int(request, "id");
  const auto url = json::get_string(request, "url");
  const auto path = to_full_path(json::get_string_list(request, "path"));
  const auto refresh = json::try_get_string(request, "refresh");
  const auto allow_lossy_compression = json::try_get_bool(request, "allowLossyCompression");
  const auto deterministic = json::try_get_bool(request, "deterministic");

  create_directories_handle_symlinks(path.parent_path());
  auto arguments = std::vector<std::string>{
    webrecorder_path().u8string(),
    "--url", '\"' + std::string(url) + '\"',
    "--file", '\"' + path.filename().u8string() + '\"',
    "--no-open-browser",
  };

  arguments.push_back("--refresh");
  if (refresh == "standard")
    arguments.push_back("when-expired");
  else if (refresh == "lazy")
    arguments.push_back("when-expired-async");
  else if (refresh == "always")
    arguments.push_back("always");
  else
    arguments.push_back("never");

  if (allow_lossy_compression)
    arguments.push_back("--allow-lossy-compression");
  if (deterministic)
    arguments.push_back("--deterministic-js");

  if (!m_block_hosts_file.empty())
    arguments.insert(end(arguments), {
      "--block-host-file", '\"' + m_block_hosts_file.u8string() + '\"',
    });
  if (!m_bypass_hosts_file.empty())
    arguments.insert(end(arguments), {
      "--host-bypass-file", '\"' + m_bypass_hosts_file.u8string() + '\"',
    });

  m_webrecorders.emplace(std::piecewise_construct,
    std::forward_as_tuple(id),
    std::forward_as_tuple(std::move(arguments), path.parent_path().u8string()));
}

void Logic::stop_recording(Response&, const Request& request) {
  const auto id = json::get_int(request, "id");
  if (auto it = m_webrecorders.find(id); it != m_webrecorders.end())
    it->second.stop();
}

void Logic::get_recording_output(Response& response, const Request& request) {
  const auto id = json::get_int(request, "id");
  if (auto it = m_webrecorders.find(id); it != m_webrecorders.end()) {
    response.Key("events");
    response.StartArray();
    it->second.for_each_output_line([&](const auto& line) { response.String(line); });
    response.EndArray();

    // cleanup stopped recorder
    if (it->second.finished()) {
      m_webrecorders.erase(it);
      return;
    }
  }
}

void Logic::set_library_root(Response& response, const Request& request) {
  const auto path = json::try_get_string(request, "path");
  auto library_root = std::filesystem::u8path(path.value_or("")).lexically_normal();

  // reset to default
  auto error = std::error_code();
  if (library_root.empty() ||
      !std::filesystem::exists(library_root, error)) {
    library_root = default_library_root();
    create_directories_handle_symlinks(library_root);
  }
  // succeeded
  m_library_root = library_root;

  response.Key("path");
  response.String(library_root.u8string());
}

void Logic::browse_directories(Response& response, const Request& request) {
  auto initial_path = std::string();
  if (const auto path = json::try_get_string(request, "path"))
    initial_path = path.value();
  if (const auto path = noc_file_dialog_open(NOC_FILE_DIALOG_DIR,
      nullptr, (initial_path.empty() ? nullptr : initial_path.c_str()), nullptr)) {
    response.Key("path");
    response.String(path);
  }
}

void Logic::set_hosts_list(Response&, const Request& request) {
  const auto list = json::get_string(request, "list");
  const auto append = json::try_get_bool(request, "append").value_or(false);
  const auto type = json::try_get_string(request, "type").value_or("block");
  auto& filename = (type == "bypass" ? m_bypass_hosts_file : m_block_hosts_file);
  if (filename.empty())
    filename = generate_temporary_filename();
  auto file = std::ofstream(filename,
    std::ios::binary | (append ? std::ios::app : std::ios::out));
  file.write(list.data(), static_cast<std::streamsize>(list.size()));
}

void Logic::get_file_size(Response& response, const Request& request) {
  const auto path = to_full_path(json::get_string_list(request, "path"));
  if (std::filesystem::is_regular_file(path)) {
    const auto file_size = std::filesystem::file_size(path);
    response.Key("fileSize");
    response.Uint64(file_size);
  }
}

Database& Logic::database() {
  if (m_library_root.empty())
    throw std::runtime_error("library root not set");
  if (!m_database)
    m_database = std::make_unique<Database>(m_library_root / index_database_filename);
  return *m_database;
}

void Logic::update_search_index(Response&, const Request& request) {
  const auto path = to_full_path(json::get_string_list(request, "path"));
  database().update_index(path);
}

void Logic::execute_search(Response& response, const Request& request) {
  const auto query = json::get_string(request, "query");
  const auto highlight = json::try_get_bool(request, "highlight").value_or(false);
  const auto snippet_size = json::try_get_int(request, "snippetSize").value_or(16);
  const auto max_count = json::try_get_int(request, "maxCount").value_or(5);
  response.Key("matches");
  response.StartArray();
  database().execute_search(query, highlight, snippet_size, max_count,
    [&](SearchResult r) {
      response.StartObject();
      response.String("uid");
      response.Int64(r.uid);
      response.String("url");
      response.String(r.url.data(), static_cast<json::size_t>(r.url.size()));
      response.String("title");
      response.String(r.title.data(), static_cast<json::size_t>(r.title.size()));
      response.String("snippet");
      response.String(r.snippet.data(), static_cast<json::size_t>(r.snippet.size()));
      response.EndObject();
    });
  response.EndArray();
}

void Logic::handle_request(Response& response, const Request& request) {
  using Handler = void(Logic::*)(Response&, const Request&);
  static const auto s_action_handlers = std::map<std::string_view, Handler> {
    { "moveFile", &Logic::move_file },
    { "deleteFile", &Logic::delete_file },
    { "undeleteFile", &Logic::undelete_file },
    { "startRecording", &Logic::start_recording },
    { "stopRecording", &Logic::stop_recording },
    { "getRecordingOutput", &Logic::get_recording_output },
    { "setLibraryRoot", &Logic::set_library_root },
    { "browserDirectories", &Logic::browse_directories },
    { "setHostList", &Logic::set_hosts_list },
    { "getFileSize", &Logic::get_file_size },
    { "updateSearchIndex", &Logic::update_search_index },
    { "executeSearch", &Logic::execute_search },
  };
  const auto action = json::get_string(request, "action");
  const auto it = s_action_handlers.find(action);
  if (it == s_action_handlers.end())
    throw std::runtime_error("invalid action " + std::string(action));
  const auto handler = it->second;
  (this->*handler)(response, request);
}

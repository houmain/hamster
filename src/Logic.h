#pragma once

#include "Settings.h"
#include "Webrecorder.h"
#include "Json.h"
#include <map>

using Response = json::Writer;
using Request = json::Document;
class Database;
class BackgroundWorker;

class Logic {
public:
  explicit Logic(const Settings& settings);
  Logic(Logic&) = delete;
  Logic& operator=(Logic&) = delete;
  ~Logic();

  void handle_request(Response& response, const Request& request);

private:
  std::filesystem::path to_full_path(const std::vector<std::string_view>& strings) const;
  void get_status(Response& response, const Request&);
  void move_file(Response&, const Request& request);
  void delete_file(Response&, const Request& request);
  void undelete_file(Response&, const Request& request);
  void start_recording(Response& response, const Request& request);
  void stop_recording(Response&, const Request& request);
  void get_recording_output(Response& response, const Request& request);
  void set_library_root(Response& response, const Request& request);
  void browse_directories(Response& response, const Request& request);
  void set_temporary_file(std::filesystem::path& path, std::string_view content);
  void inject_script(Response&, const Request& request);
  void set_block_hosts_list(Response&, const Request& request);
  void get_file_size(Response& response, const Request& request);
  void get_file_listing(Response& response, const Request& request);
  BackgroundWorker& background_worker();
  Database& database();
  void update_search_index(Response&, const Request& request);
  void execute_search(Response& response, const Request& request);

  const Settings& m_settings;
  std::unique_ptr<Database> m_database;
  std::filesystem::path m_inject_script_file;
  std::filesystem::path m_block_hosts_file;
  std::filesystem::path m_library_root;
  std::map<int, Webrecorder> m_webrecorders;
  std::unique_ptr<BackgroundWorker> m_background_worker;
};

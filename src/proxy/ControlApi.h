#pragma once

#include "ProxyRequest.h"
#include <string>

class Catalog;
class Library;
class RecordingCoordinator;

struct ControlResponse {
  StatusCode status{ StatusCode::success_ok };
  std::string body;
  Header headers;
};

class ControlApi final {
public:
  ControlApi(Library& library, Catalog& catalog, RecordingCoordinator& recordings);

  void set_proxy_port(unsigned short port);
  ControlResponse handle(const std::string& path, const std::string& content);

private:
  ControlResponse status();
  ControlResponse version();
  ControlResponse stop();
  ControlResponse finish_recording(const Header& fields);
  ControlResponse finish_all_recordings();
  ControlResponse finish_unlisted_recordings(const Header& fields);
  ControlResponse set_recording_options(const Header& fields);
  ControlResponse replace_bookmarks(const Header& fields);
  ControlResponse update_bookmarks(const Header& fields);
  ControlResponse get_search_index_status();
  ControlResponse execute_search(const Header& fields);
  ControlResponse update_search_index(const Header& fields);
  ControlResponse rebuild_search_index();
  ControlResponse get_archive_size(const Header& fields);
  ControlResponse move_archive(const Header& fields);
  ControlResponse delete_archive(const Header& fields);
  ControlResponse restore_archive(const Header& fields);
  ControlResponse get_archive_files(const Header& fields);
  ControlResponse get_archive_list();

  Library& m_library;
  Catalog& m_catalog;
  RecordingCoordinator& m_recordings;
  unsigned short m_proxy_port{};
};

#pragma once

#include "ResourceGraph.h"
#include "SessionController.h"
#include "Settings.h"
#include "catalog/Catalog.h"
#include "proxy/Client.h"
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

class Catalog;
class Library;
class ProxyRequest;
class ResponseJournal;

class RecordingCoordinator final {
public:
  RecordingCoordinator(const Settings& settings, Library& library, Catalog& catalog);
  ~RecordingCoordinator();

  void set_interception_changed(std::function<void()> callback);
  void handle_request(ProxyRequest request);

  bool close_owner(const ResourceOwner& owner);
  bool close_all_owners();
  bool close_inactive(const std::set<std::string>& active_bookmarks);
  void set_recording_options(
    std::string serve, std::string download, bool allow_lossy_compression);
  void catalog_changed();

  bool should_intercept(const std::string& authority) const;
  void expire();
  void begin_stop();
  bool stopping() const;
  bool drain();

private:
  friend struct RecordingCoordinatorTestAccess;

  struct MatchedSession {
    ResourceOwner owner;
    std::string id;
    SessionController::Lease lease;
  };

  void finish_session(const std::string& id);
  std::string open(RecordingSession::Settings session, const ResourceOwner& owner);
  std::string open_catalog_match(const Bookmark& bookmark, const std::string& request_url);
  void index_archive(const std::filesystem::path& archive);
  bool close(std::string id);
  void close_obsolete_catalog_sessions();
  void handle_observer_request(const std::string& id, ProxyRequest request);
  void forward(ProxyRequest request);

  Settings m_settings;
  Library& m_library;
  Catalog& m_catalog;
  SessionController m_sessions;
  Client m_upstream;
  std::shared_ptr<int> m_active{std::make_shared<int>(0)};
  std::shared_ptr<ResourceGraph> m_resource_graph{std::make_shared<ResourceGraph>()};
  std::shared_ptr<ResponseJournal> m_response_journal;
  std::map<std::string, ResourceOwner> m_session_owners;
  std::map<ResourceOwner, std::string> m_owner_sessions;
  std::map<ResourceOwner, Bookmark> m_owner_bookmarks;
  std::map<std::string, std::chrono::steady_clock::time_point> m_leases;
  std::string m_serve{"latest"};
  std::string m_download{"standard"};
  bool m_allow_lossy_compression{};
  std::function<void()> m_interception_changed = [] {};
  bool m_is_stopping{};
};

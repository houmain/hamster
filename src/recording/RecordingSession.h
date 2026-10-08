#pragma once

#include "proxy/ProxyRequest.h"
#include "proxy/Client.h"
#include "library/HeaderStore.h"
#include "library/Archive.h"
#include <chrono>
#include <map>
#include <optional>
#include <set>
#include <vector>
#include "CacheInfo.h"

class HostList;

enum class DownloadPolicy {
  standard, // not yet archived or expired
  always,   // ignore version of archived
  never,    // do not download
};

enum class ServePolicy {
  latest,         // wait until archived version was validated
  last_archived,  // serve archived before it was validated
  first_archived, // serve first archived version
};

enum class ArchivePolicy {
  latest,           // update latest version of file
  first,            // do not update files
  latest_and_first, // keep first and update latest version of file
  requested,        // store only currently requested
};

class RecordingSession final {
public:
  struct Settings {
    std::string url;
    std::filesystem::path input_file, output_file;
    std::vector<std::filesystem::path> block_hosts_files;
    bool allow_lossy_compression{};
    DownloadPolicy download_policy{};
    ServePolicy serve_policy{};
    ArchivePolicy archive_policy{};
    std::chrono::seconds refresh_timeout{1};
    std::chrono::seconds request_timeout{5};
    std::string proxy_server;
    std::string upstream_ca;
    std::string impersonate{ "firefox147" };
  };

  static Settings parse_settings(const Header& fields);
  explicit RecordingSession(Settings settings);
  RecordingSession(const RecordingSession&) = delete;
  RecordingSession& operator=(const RecordingSession&) = delete;
  ~RecordingSession();

  const std::string& url() const;

  // Stop dispatching requests and drain pending handlers before calling finish.
  // Commits this session's archive without stopping the shared server/context.
  // Idempotent; the destructor provides a finalization fallback.
  void finish();

  // threadsafe
  void handle_request(ProxyRequest request);
  // Store a response fetched by another matching session without sending it.
  void record_response(const std::string& identifying_url, StatusCode status_code,
    const Header& header, ByteView data, time_t response_time);

private:
  void initialize();
  void serve_blocked(ProxyRequest& request, const std::string& url);
  void serve_error(ProxyRequest& request, const std::string& url, StatusCode status_code);
  void handle_file_request(ProxyRequest request, const std::string& url);
  void forward_request(
    ProxyRequest request, const std::string& url, const std::optional<CacheInfo>& cache_info);
  void handle_response(ProxyRequest& request, const std::string& url, Client::Response response);
  [[nodiscard]] bool serve_previously_served(ProxyRequest& request, const std::string& url);
  [[nodiscard]] bool serve_from_archive(
    ProxyRequest& request, const std::string& url, bool write_to_archive);
  void serve_file(ProxyRequest& request, const std::string& url, StatusCode status_code,
    const Header& header, ByteView data, time_t response_time);
  void async_write_file(const std::string& identifying_url, StatusCode status_code,
    const Header& header, ByteView data, time_t response_time, bool allow_lossy_compression,
    std::function<void(bool)>&& on_complete);
  std::optional<std::string> archive_target_url(
    const std::string& identifying_url, StatusCode status_code, const Header& header);
  void append_unrequested_files();

  // only updated while single threaded
  Settings m_settings;
  bool m_finished{};
  std::string m_uid;
  std::unique_ptr<ArchiveReader> m_archive_reader;
  std::unique_ptr<HostList> m_blocked_hosts;
  HeaderStore m_header_reader;

  // threadsafe
  Client m_client;

  // modifications sequenced by mutex
  mutable std::mutex m_write_mutex;
  std::unique_ptr<ArchiveWriter> m_archive_writer;
  HeaderStore m_header_writer;

  mutable std::mutex m_navigation_mutex;
  std::map<std::string, std::string> m_redirect_sources;
  std::set<std::string> m_pending_documents;
};

struct FileRequestAction {
  bool serve;
  bool write;
  bool download;
};

std::filesystem::path resolve_archive_path(const std::filesystem::path& path);
FileRequestAction get_file_request_action(
  const RecordingSession::Settings& settings, bool archived, bool expired);

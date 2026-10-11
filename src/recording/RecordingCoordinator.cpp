#include "RecordingCoordinator.h"
#include "ResponseJournal.h"
#include "ResourceGraph.h"
#include "SessionController.h"
#include "catalog/Catalog.h"
#include "decode_content.h"
#include "library/Indexing.h"
#include "library/Library.h"
#include "proxy/Client.h"
#include "proxy/ProxyRequest.h"
#include "Settings.h"
#include "common.h"
#include "platform.h"
#include <openssl/rand.h>
#include <array>
#include <chrono>
#include <ctime>
#include <map>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

namespace {
  constexpr auto observer_path = std::string_view("/.hamster-observer/");

  constexpr auto resource_observer_script =
#include "ResourceObserverScript.inc"
    ;

  char ascii_lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  }

  bool ascii_iequal(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
      return false;
    for (auto i = size_t{}; i < a.size(); ++i)
      if (ascii_lower(a[i]) != ascii_lower(b[i]))
        return false;
    return true;
  }

  size_t ascii_ifind(std::string_view text, std::string_view wanted, size_t offset = 0) {
    if (wanted.empty())
      return offset <= text.size() ? offset : std::string_view::npos;
    for (; offset + wanted.size() <= text.size(); ++offset)
      if (ascii_iequal(text.substr(offset, wanted.size()), wanted))
        return offset;
    return std::string_view::npos;
  }

  std::string add_csp_source(std::string policy, std::string_view directive,
    std::string_view source, std::string_view fallback = {},
    bool same_origin_source = false) {
    auto parts = std::vector<std::string>{};
    for (auto begin = size_t{}; begin <= policy.size();) {
      const auto semicolon = policy.find(';', begin);
      const auto end = semicolon == std::string::npos ? policy.size() : semicolon;
      const auto part = trim(std::string_view(policy).substr(begin, end - begin));
      if (!part.empty())
        parts.emplace_back(part);
      if (semicolon == std::string::npos)
        break;
      begin = semicolon + 1;
    }
    auto find_directive = [&](std::string_view name) -> std::string* {
      for (auto& part : parts) {
        const auto space = part.find_first_of(" \t\r\n");
        if (ascii_iequal(std::string_view(part).substr(0, space), name))
          return &part;
      }
      return nullptr;
      };
    const auto allows_same_origin = [](const std::string& part) {
      if (part.find("'strict-dynamic'") != std::string::npos)
        return false;
      return part.find("'self'") != std::string::npos ||
        part.find('*') != std::string::npos;
      };
    if (auto part = find_directive(directive)) {
      if (same_origin_source && allows_same_origin(*part))
        return policy;
      if (part->find(source) == std::string::npos)
        *part += " " + std::string(source);
    }
    else {
      auto value = std::string{};
      const auto inherited = fallback.empty() ? nullptr : find_directive(fallback);
      if (!inherited)
        return policy;
      if (same_origin_source && allows_same_origin(*inherited))
        return policy;
      const auto space = inherited->find_first_of(" \t\r\n");
      if (space != std::string::npos)
        value = inherited->substr(space);
      parts.emplace_back(std::string(directive) + value + " " + std::string(source));
    }
    auto result = std::string{};
    for (const auto& part : parts) {
      if (!result.empty())
        result += "; ";
      result += part;
    }
    return result;
  }

  std::string patch_csp(std::string policy, const std::string& token) {
    const auto nonce = "'nonce-" + token + "'";
    auto has_script_element = false;
    for (auto begin = size_t{}; begin < policy.size();) {
      const auto end = policy.find(';', begin);
      const auto part = trim(std::string_view(policy).substr(
        begin, (end == std::string::npos ? policy.size() : end) - begin));
      const auto space = part.find_first_of(" \t\r\n");
      if (ascii_iequal(part.substr(0, space), "script-src-elem")) {
        has_script_element = true;
        break;
      }
      if (end == std::string::npos)
        break;
      begin = end + 1;
    }
    policy = add_csp_source(std::move(policy),
      has_script_element ? "script-src-elem" : "script-src", nonce, "default-src", true);
    return add_csp_source(
      std::move(policy), "connect-src", "'self'", "default-src", true);
  }

  void patch_csp_headers(Header& header, const std::string& token) {
    auto policies = std::vector<std::string>{};
    const auto range = header.equal_range("Content-Security-Policy");
    for (auto it = range.first; it != range.second; ++it)
      policies.push_back(patch_csp(it->second, token));
    header.erase("Content-Security-Policy");
    for (auto& policy : policies)
      header.emplace("Content-Security-Policy", std::move(policy));
  }

  std::vector<std::string> discover_response_resources(const std::string& response_url,
    StatusCode status_code, const Header& header, ByteView data) {
    auto resources = std::set<std::string>{};
    if (is_redirect(status_code))
      if (const auto location = header.find("Location"); location != header.end())
        if (auto resolved = resolve_http_url(response_url, location->second))
          resources.insert(std::move(*resolved));
    const auto content_type = header.find("Content-Type");
    if (content_type == header.end() || data.empty())
      return { resources.begin(), resources.end() };
    const auto [mime_type, charset] = split_content_type(content_type->second);
    if (!iequals_any(mime_type, "text/html", "application/xhtml+xml", "text/css",
      "application/json", "text/json", "text/javascript", "application/javascript"))
      return { resources.begin(), resources.end() };
    const auto encoding = header.find("Content-Encoding");
    const auto decoded = decode_content(data, encoding == header.end() ? "" : encoding->second);
    if (!decoded)
      return { resources.begin(), resources.end() };
    const auto text = charset.empty() ? *decoded : convert_charset(*decoded, charset, "UTF-8");
    auto add = [&](std::string resource) { resources.insert(std::move(resource)); };
    if (iequals(mime_type, "text/css"))
      for_each_css_resources(response_url, text, add);
    else if (iequals_any(mime_type,
      "application/json", "text/json", "text/javascript", "application/javascript"))
      for_each_json_resource(response_url, text, add);
    else
      for_each_html_resource(response_url, text, add);
    return { resources.begin(), resources.end() };
  }

  bool response_resources_should_be_prefetched(const Header& header) {
    const auto content_type = header.find("Content-Type");
    if (content_type == header.end())
      return false;
    const auto mime_type = split_content_type(content_type->second).first;
    return iequals_any(mime_type,
      "application/json", "text/json", "text/javascript", "application/javascript");
  }

  std::optional<ProxyRequest::TransformedResponse> inject_resource_observer(
    const Header& header, ByteView data, const std::string& observer_token) {
    const auto content_type = header.find("Content-Type");
    if (content_type == header.end() || data.empty() ||
      header.count("Content-Range") ||
      !iequals(split_content_type(content_type->second).first, "text/html"))
      return {};
    const auto encoding = header.find("Content-Encoding");
    auto html = decode_content(data, encoding == header.end() ? "" : encoding->second);
    if (!html)
      return {};

    const auto script = std::string("<script nonce=\"") + observer_token +
      "\" src=\"" + std::string(observer_path) + observer_token + "\"></script>";

    auto insertion = size_t{};
    if (const auto head = ascii_ifind(*html, "<head"); head != std::string::npos) {
      const auto close = html->find('>', head + 5);
      if (close != std::string::npos)
        insertion = close + 1;
    }
    else if (const auto doctype = ascii_ifind(*html, "<!doctype");
      doctype != std::string::npos) {
      const auto close = html->find('>', doctype + 9);
      if (close != std::string::npos)
        insertion = close + 1;
    }
    html->insert(insertion, script);

    auto result = ProxyRequest::TransformedResponse{ header, {} };
    result.header.erase("Content-Encoding");
    result.header.erase("Content-Length");
    result.header.erase("Content-MD5");
    result.header.erase("Digest");
    result.header.emplace("Content-Length", std::to_string(html->size()));
    patch_csp_headers(result.header, observer_token);
    const auto bytes = as_byte_view(*html);
    result.data.assign(bytes.begin(), bytes.end());
    return result;
  }

  std::optional<std::string> observer_session_id(const std::string& url) {
    const auto target = get_scheme_hostname_port_path(url);
    const auto marker = target.find(observer_path);
    if (marker == std::string_view::npos)
      return {};
    const auto token = target.substr(marker + observer_path.size());
    if (token.size() != 48 ||
      token.find_first_not_of("0123456789abcdefABCDEF") != std::string_view::npos)
      return {};
    return std::string(token);
  }

  std::string generate_session_id() {
    static constexpr auto hex = "0123456789abcdef";
    auto random = std::array<unsigned char, 24>{};
    if (RAND_bytes(random.data(), static_cast<int>(random.size())) != 1)
      throw std::runtime_error("Generating session ID failed");
    auto id = std::string{};
    for (auto byte : random) {
      id += hex[byte >> 4];
      id += hex[byte & 15];
    }
    return id;
  }
} // namespace

void RecordingCoordinator::finish_session(const std::string& id) {
  const auto found = m_session_owners.find(id);
  if (found == m_session_owners.end())
    return;
  m_resource_graph->release(found->second);
  m_owner_sessions.erase(found->second);
  m_owner_bookmarks.erase(found->second);
  m_session_owners.erase(found);
  if (m_session_owners.empty())
    m_response_journal->clear();
  m_interception_changed();
}

std::string RecordingCoordinator::open(
  RecordingSession::Settings session, const ResourceOwner& owner) {
  if (const auto existing = m_owner_sessions.find(owner); existing != m_owner_sessions.end())
    if (m_sessions.acquire(existing->second))
      return existing->second;

  session.input_file = m_library.path(path_to_utf8(session.input_file));
  session.output_file = m_library.path(path_to_utf8(session.output_file));
  if (!session.output_file.empty())
    std::filesystem::create_directories(session.output_file.parent_path());
  const auto input = session.input_file;
  const auto output = session.output_file;
  const auto root_url = session.url;
  const auto id = generate_session_id();
  m_sessions.open(id, std::move(session));
  m_resource_graph->claim(owner, root_url);
  m_session_owners.emplace(id, owner);
  m_owner_sessions.emplace(owner, id);
  if (const auto bookmark = m_catalog.find_bookmark(owner.bookmark))
    m_owner_bookmarks.emplace(owner, *bookmark);
  m_interception_changed();
  m_leases[id] = std::chrono::steady_clock::now();
  m_library.opened(id, input, output);
  return id;
}

std::string RecordingCoordinator::open_catalog_match(
  const Bookmark& bookmark, const std::string& request_url) {
  const auto owner = ResourceOwner{ bookmark.path };
  if (const auto existing = m_owner_sessions.find(owner); existing != m_owner_sessions.end())
    if (m_sessions.acquire(existing->second)) {
      m_resource_graph->claim(owner, request_url);
      return existing->second;
    }
  auto fields = Header{ {m_download == "never" ? "input" : "file", bookmark.path},
    {"url", bookmark.url}, {"download", m_download},
    {"serve", m_serve}, {"archive", "latest-and-first"} };
  if (m_allow_lossy_compression)
    fields.emplace("allow-lossy-compression", "true");
  auto session = RecordingSession::parse_settings(fields);
  session.proxy_server = m_settings.proxy_server;
  session.upstream_ca = m_settings.upstream_ca;
  session.impersonate = m_settings.impersonate;
  session.request_timeout = m_settings.request_timeout;
  const auto id = open(std::move(session), owner);
  if (m_download == "never")
    for (const auto& file : m_catalog.get_archive_files(m_library.path(bookmark.path)))
      m_resource_graph->claim(owner, file.url);
  m_resource_graph->claim(owner, request_url);
  log(Event::info, "Recording bookmark ", bookmark.path,
    " for ", request_url);
  return id;
}

void RecordingCoordinator::index_archive(const std::filesystem::path& archive) {
  if (archive.empty())
    return;
  try {
    m_catalog.update_search_index(archive);
  }
  catch (const std::exception& ex) {
    log(Event::error, "Queueing search index: ", ex.what());
  }
}

bool RecordingCoordinator::close(std::string id) {
  const auto complete = m_sessions.close(id);
  if (complete) {
    finish_session(id);
    m_leases.erase(id);
    index_archive(m_library.committed(id));
  }
  return complete;
}

void RecordingCoordinator::close_obsolete_catalog_sessions() {
  auto obsolete = std::vector<std::string>{};
  for (const auto& [id, owner] : m_session_owners) {
    const auto current = m_catalog.find_bookmark(owner.bookmark);
    const auto opened = m_owner_bookmarks.find(owner);
    if (!current || opened == m_owner_bookmarks.end() || !(opened->second == *current))
      obsolete.push_back(id);
  }
  for (const auto& id : obsolete)
    if (!close(id))
      m_leases[id] = std::chrono::steady_clock::time_point::min();
}

void RecordingCoordinator::handle_observer_request(const std::string& id, ProxyRequest request) {
  const auto owner = m_session_owners.find(id);
  auto lease = m_sessions.acquire(id);
  if (owner == m_session_owners.end() || !lease) {
    request.send_response(StatusCode::client_error_not_found, {}, {});
    return;
  }
  if (request.method() == "GET") {
    const auto script = std::string_view(resource_observer_script);
    request.send_response(StatusCode::success_ok,
      { {"Cache-Control", "no-store"},
        {"Content-Type", "text/javascript; charset=utf-8"},
        {"Content-Length", std::to_string(script.size())} },
      as_byte_view(script));
    return;
  }
  if (request.method() != "POST") {
    request.send_response(StatusCode::client_error_method_not_allowed, {}, {});
    return;
  }
  if (request.data().size() > 65536) {
    request.send_response(StatusCode::client_error_payload_too_large, {}, {});
    return;
  }
  auto input = std::istringstream(std::string(as_string_view(request.data())));
  auto reported_url = std::string{};
  auto count = size_t{};
  auto claimed = false;
  auto loaded = false;
  while (std::getline(input, reported_url)) {
    if (++count > 512)
      break;
    if (!reported_url.empty() && reported_url.back() == '\r')
      reported_url.pop_back();
    if (reported_url == "@loaded") {
      loaded = true;
      continue;
    }
    if (reported_url.empty() || reported_url.size() > 8192)
      continue;
    try {
      log(Event::resource, owner->second.bookmark, " ", reported_url);
      claimed = m_resource_graph->claim(owner->second, reported_url) || claimed;
      for (const auto& response : m_response_journal->find(reported_url))
        lease->record_response(response.identifying_url, response.status_code, response.header,
          *response.data, response.response_time);
    }
    catch (const std::exception& ex) {
      log(Event::error, "Late resource attribution: ", ex.what());
    }
  }
  if (claimed)
    m_interception_changed();
  m_leases[id] = std::chrono::steady_clock::now();
  const auto loaded_bookmark = loaded ? owner->second.bookmark : std::string{};
  request.send_response(StatusCode::success_no_content, { {"Cache-Control", "no-store"} }, {},
    [loaded_bookmark] {
      if (!loaded_bookmark.empty())
        log(Event::loaded, loaded_bookmark);
    });
}

void RecordingCoordinator::forward(ProxyRequest request) {
  auto pending = std::make_shared<ProxyRequest>(std::move(request));
  auto headers = pending->header();
  headers.erase("Host");
  headers.erase("Content-Length");
  headers.emplace("Connection", "close");
  m_upstream.request(pending->target_url(), pending->method(), std::move(headers),
    pending->data(), m_settings.request_timeout, [pending](Client::Response response) {
      if (response.error()) {
        log(Event::forward_failed, response.error_message(), " ", pending->target_url());
        pending->send_response(StatusCode::server_error_bad_gateway, {}, {});
        return;
      }
      auto headers = response.header();
      strip_proxy_headers(headers);
      if (pending->method() != "HEAD" &&
        response.status_code() != StatusCode::redirection_not_modified) {
        headers.erase("Content-Length");
        if (response.status_code() != StatusCode::success_no_content)
          headers.emplace("Content-Length", std::to_string(response.data().size()));
      }
      pending->send_response(response.status_code(), headers, response.data());
    });
}

void RecordingCoordinator::handle_request(ProxyRequest request) {
  if (m_is_stopping) {
    request.send_response(StatusCode::server_error_service_unavailable, {}, {});
    return;
  }
  request.hold_lifetime(m_active);
  if (const auto observer_id = observer_session_id(request.target_url())) {
    handle_observer_request(*observer_id, std::move(request));
    return;
  }
  for (const auto& bookmark : m_catalog.find_bookmarks_by_url(request.target_url()))
    try {
      open_catalog_match(bookmark, request.target_url());
    }
  catch (const std::exception& ex) {
    log(Event::error, "Starting recording: ", ex.what());
  }

  const auto graph_owners = m_resource_graph->owners(request.target_url());
  auto matched_sessions = std::vector<MatchedSession>{};
  for (const auto& owner : graph_owners)
    if (const auto found = m_owner_sessions.find(owner); found != m_owner_sessions.end())
      if (auto lease = m_sessions.acquire(found->second))
        matched_sessions.push_back({ owner, found->second, std::move(lease) });
  for (const auto& matched : matched_sessions)
    m_leases[matched.id] = std::chrono::steady_clock::now();

  if (!m_session_owners.empty())
    request.transform_response_headers([](Header& header) {
    header.erase("Cache-Control");
    header.emplace("Cache-Control", "no-store");
      });

  const auto primary_id =
    matched_sessions.empty() ? std::string{} : matched_sessions.front().id;
  const auto destination = request.header().find("Sec-Fetch-Dest");
  const auto document_request = destination != request.header().end() &&
    iequals_any(destination->second, "document", "iframe", "frame");
  if (document_request && !primary_id.empty() && m_session_owners.count(primary_id))
    request.transform_response([primary_id](StatusCode status, const Header& header,
      ByteView data) -> std::optional<ProxyRequest::TransformedResponse> {
        if (!is_success(status))
          return std::nullopt;
        return inject_resource_observer(header, data, primary_id);
      });

  if (!matched_sessions.empty() && request.method() != "HEAD") {
    const auto response_url = request.target_url();
    const auto identifying_url = get_identifying_url(request.target_url(), request.data());
    request.observe_response([graph = m_resource_graph, matched_sessions, primary_id, response_url,
      identifying_url](StatusCode status, const Header& header,
        ByteView data) {
          try {
            const auto response_time = std::time(nullptr);
            const auto discovered = discover_response_resources(response_url, status, header, data);
            const auto prefetch = response_resources_should_be_prefetched(header);
            for (const auto& matched : matched_sessions) {
              for (const auto& url : discovered) {
                graph->claim(matched.owner, url);
                if (prefetch) {
                  auto request = ProxyRequest("GET", {}, {}, { {"Referer", response_url} }, {}, {}, url);
                  request.hold_lifetime(matched.lease);
                  matched.lease->handle_request(std::move(request));
                }
              }
              if (matched.id != primary_id)
                matched.lease->record_response(
                  identifying_url, status, header, data, response_time);
            }
          }
          catch (const std::exception& ex) {
            log(Event::error, "Attributing response: ", ex.what());
          }
          catch (...) {
            log(Event::error, "Attributing response: unknown failure");
          }
      });
  }
  else if (primary_id.empty() && request.method() != "HEAD") {
    const auto response_url = request.target_url();
    const auto request_url = get_identifying_url(request.target_url(), request.data());
    request.observe_response([journal = m_response_journal, response_url, request_url](
      StatusCode status, const Header& header, ByteView data) {
        try {
          journal->store(response_url, request_url, status, header, data, std::time(nullptr));
        }
        catch (const std::exception& ex) {
          log(Event::error, "Journaling unassigned response: ", ex.what());
        }
      });
  }

  if (!primary_id.empty()) {
    m_sessions.handle_request(primary_id, std::move(request));
    return;
  }
  forward(std::move(request));
}

bool RecordingCoordinator::close_owner(const ResourceOwner& owner) {
  const auto found = m_owner_sessions.find(owner);
  return found == m_owner_sessions.end() || close(found->second);
}

bool RecordingCoordinator::close_all_owners() {
  auto ids = std::vector<std::string>{};
  for (const auto& [id, owner] : m_session_owners)
    ids.push_back(id);
  auto complete = true;
  for (const auto& id : ids)
    if (!close(id))
      complete = false;
  return complete;
}

bool RecordingCoordinator::close_inactive(
  const std::set<std::string>& active_bookmarks) {
  auto ids = std::vector<std::string>{};
  for (const auto& [id, owner] : m_session_owners)
    if (!active_bookmarks.count(owner.bookmark))
      ids.push_back(id);
  auto complete = true;
  for (const auto& id : ids)
    if (!close(id))
      complete = false;
  return complete;
}

bool RecordingCoordinator::should_intercept(const std::string& authority) const {
  if (!m_settings.proxy_server.empty())
    return true;
  return !m_session_owners.empty() ||
    m_catalog.contains_bookmark_for_https_authority(authority) ||
    m_resource_graph->contains_https_authority(authority);
}

void RecordingCoordinator::expire() {
  for (auto it = m_leases.begin(); it != m_leases.end();) {
    if (std::chrono::steady_clock::now() - it->second > std::chrono::seconds(90)) {
      try {
        const auto id = it->first;
        if (close(id)) {
          it = m_leases.lower_bound(id);
          continue;
        }
      }
      catch (const std::exception& ex) {
        log(Event::error, "Closing expired recording: ", ex.what());
      }
    }
    ++it;
  }
}

bool RecordingCoordinator::drain() {
  const auto complete = m_sessions.close_all();
  if (!complete || m_active.use_count() != 1)
    return false;
  while (!m_session_owners.empty())
    finish_session(m_session_owners.begin()->first);
  for (const auto& archive : m_library.committed_all())
    index_archive(archive);
  return true;
}

RecordingCoordinator::RecordingCoordinator(
  const Settings& settings_, Library& library_, Catalog& catalog_)
  : m_settings(settings_), m_library(library_), m_catalog(catalog_),
  m_upstream(m_settings.proxy_server, m_settings.upstream_ca, m_settings.impersonate),
  m_response_journal(std::make_shared<ResponseJournal>()) {}

RecordingCoordinator::~RecordingCoordinator() = default;

void RecordingCoordinator::set_interception_changed(std::function<void()> callback) {
  m_interception_changed = std::move(callback);
}

void RecordingCoordinator::set_recording_options(
  std::string serve_, std::string download_, bool allow_lossy_compression_) {
  m_serve = std::move(serve_);
  m_download = std::move(download_);
  m_allow_lossy_compression = allow_lossy_compression_;
}

void RecordingCoordinator::catalog_changed() {
  m_interception_changed();
  close_obsolete_catalog_sessions();
}

void RecordingCoordinator::begin_stop() {
  m_is_stopping = true;
}

bool RecordingCoordinator::stopping() const {
  return m_is_stopping;
}

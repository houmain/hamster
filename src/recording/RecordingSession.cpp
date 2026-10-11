
#include "RecordingSession.h"
#include "HostList.h"
#include "common.h"
#include "library/LossyCompressor.h"
#include "platform.h"
#include <sstream>
#include <utility>
#include <cassert>
#include <regex>

namespace {
  const auto basic_text_header = Header{ {"Content-Type", "text/javascript;charset=utf-8"} };
  const auto first_overlay_path = "first/";

  bool is_document_response(const Header& header) {
    const auto content_type = header.find("Content-Type");
    if (content_type == header.end())
      return false;
    const auto mime_type = split_content_type(content_type->second).first;
    return iequals_any(mime_type, "text/html", "application/xhtml+xml");
  }

  bool same_document_path(const std::string& first, const std::string& second) {
    return get_scheme_hostname_port_path(first) == get_scheme_hostname_port_path(second);
  }
} // namespace

RecordingSession::Settings RecordingSession::parse_settings(const Header& fields) {
  auto result = Settings{};
  if (fields.count("file") && (fields.count("input") || fields.count("output")))
    throw std::runtime_error("Use file or input/output, not both");
  for (const auto& [name, value] : fields) {
    if (fields.count(name) != 1)
      throw std::runtime_error("Duplicate session setting: " + name);
    if (name == "url")
      result.url = url_from_input(value);
    else if (name == "file")
      result.input_file = result.output_file = utf8_to_path(value);
    else if (name == "input")
      result.input_file = utf8_to_path(value);
    else if (name == "output")
      result.output_file = utf8_to_path(value);
    else if (name == "download") {
      if (value == "standard")
        result.download_policy = DownloadPolicy::standard;
      else if (value == "always")
        result.download_policy = DownloadPolicy::always;
      else if (value == "never")
        result.download_policy = DownloadPolicy::never;
      else
        throw std::runtime_error("Invalid download policy");
    }
    else if (name == "serve") {
      if (value == "latest")
        result.serve_policy = ServePolicy::latest;
      else if (value == "last")
        result.serve_policy = ServePolicy::last_archived;
      else if (value == "first")
        result.serve_policy = ServePolicy::first_archived;
      else
        throw std::runtime_error("Invalid serve policy");
    }
    else if (name == "archive") {
      if (value == "latest")
        result.archive_policy = ArchivePolicy::latest;
      else if (value == "first")
        result.archive_policy = ArchivePolicy::first;
      else if (value == "latest-and-first")
        result.archive_policy = ArchivePolicy::latest_and_first;
      else if (value == "requested")
        result.archive_policy = ArchivePolicy::requested;
      else
        throw std::runtime_error("Invalid archive policy");
    }
    else if (name == "allow-lossy-compression") {
      if (value != "true" && value != "false")
        throw std::runtime_error("Invalid compression setting");
      result.allow_lossy_compression = value == "true";
    }
    else if (name == "refresh-timeout" || name == "request-timeout") {
      if (!std::regex_match(value, std::regex("[0-9]{1,5}")) || std::stoi(value) == 0)
        throw std::runtime_error("Invalid session timeout");
      (name == "refresh-timeout" ? result.refresh_timeout : result.request_timeout) =
        std::chrono::seconds(std::stoi(value));
    }
    else
      throw std::runtime_error("Unknown session setting: " + name);
  }
  if (result.input_file.empty() && result.output_file.empty())
    throw std::runtime_error("Missing archive path");
  return result;
}

RecordingSession::RecordingSession(Settings settings)
  : m_settings(std::move(settings)),
  m_client(m_settings.proxy_server, m_settings.upstream_ca, m_settings.impersonate) {
  initialize();
}

RecordingSession::~RecordingSession() {
  try {
    finish();
  }
  catch (const std::exception& ex) {
    log(Event::error, "Finalizing recording session: ", ex.what());
  }
}

std::filesystem::path resolve_archive_path(const std::filesystem::path& path) {
  if (path.empty())
    return path;
  const auto filename = utf8_to_path(get_legal_filename(path_to_utf8(path.filename())));
  return std::filesystem::weakly_canonical(
    std::filesystem::absolute(path.parent_path() / filename));
}

void RecordingSession::initialize() {
  // Resolve once: different sessions must not depend on process cwd changes.
  m_settings.input_file = resolve_archive_path(m_settings.input_file);
  m_settings.output_file = resolve_archive_path(m_settings.output_file);

  if (!m_settings.input_file.empty()) {
    auto archive_reader = std::make_unique<ArchiveReader>();
    if (archive_reader->open(m_settings.input_file))
      m_archive_reader = std::move(archive_reader);
  }

  if (m_archive_reader) {
    if (m_settings.url.empty())
      if (auto data = m_archive_reader->read("url"); !data.empty())
        m_settings.url = std::string(as_string_view(data));

    if (auto data = m_archive_reader->read("uid"); !data.empty())
      m_uid = std::string(as_string_view(data));

    if (auto data = m_archive_reader->read("headers"); !data.empty())
      m_header_reader.deserialize(as_string_view(data));

    if (m_settings.serve_policy == ServePolicy::first_archived)
      m_archive_reader->set_overlay_path(first_overlay_path);
  }
  if (m_uid.empty())
    m_uid = generate_id();

  if (m_settings.url.empty())
    throw std::runtime_error(
      m_settings.input_file.empty() ? "no URL specified" : "reading file failed");

  if (!m_settings.output_file.empty()) {
    m_archive_writer = std::make_unique<ArchiveWriter>();
    for (auto i = 0; i < 5; ++i)
      if (m_archive_writer->open(generate_temporary_filename("hamster-")))
        break;
    if (!m_archive_writer->is_open())
      throw std::runtime_error("opening temporary file failed");

    m_archive_writer->move_on_close(m_settings.output_file, true);
    m_archive_writer->write("uid", as_byte_view(m_uid));
    m_archive_writer->write("url", as_byte_view(m_settings.url));

    if (m_settings.allow_lossy_compression)
      m_archive_writer->set_lossy_compressor(std::make_unique<LossyCompressor>());
  }

  if (m_settings.url.empty())
    throw std::runtime_error(
      m_settings.input_file.empty() ? "no URL specified" : "reading file failed");

  auto blocked_hosts = std::make_unique<HostList>();
  for (const auto& file : m_settings.block_hosts_files)
    blocked_hosts->add_hosts_from_file(file);
  if (blocked_hosts->has_hosts())
    m_blocked_hosts = std::move(blocked_hosts);
}

void RecordingSession::finish() {
  if (m_finished)
    return;
  m_finished = true;
  append_unrequested_files();

  m_archive_reader.reset();

  if (m_archive_writer) {
    m_archive_writer->write("headers", as_byte_view(m_header_writer.serialize()));
    const auto closed = m_archive_writer->close();
    m_archive_writer.reset();
    if (!closed)
      throw std::runtime_error("Finalizing archive failed");
  }
}

void RecordingSession::handle_request(ProxyRequest request) {
  if (m_finished) {
    request.send_response(StatusCode::server_error_service_unavailable, {}, {});
    return;
  }
  const auto url = request.target_url();
  if (m_blocked_hosts && m_blocked_hosts->contains(url))
    return serve_blocked(request, url);
  handle_file_request(std::move(request), url);
}

void RecordingSession::record_response(const std::string& identifying_url,
  StatusCode status_code, const Header& header, ByteView data, time_t response_time) {
  const auto target_url = archive_target_url(identifying_url, status_code, header);
  if (!target_url)
    return;
  auto copy = std::make_shared<ByteVector>(data.begin(), data.end());
  const auto copy_view = ByteView(*copy);
  async_write_file(*target_url, status_code, header, copy_view, response_time, true,
    [copy](bool succeeded) {
      if (!succeeded)
        log(Event::writing_failed);
    });
}

void RecordingSession::serve_blocked(ProxyRequest& request, const std::string& url) {
  log(Event::download_blocked, url);
  serve_error(request, url, StatusCode::server_error_service_unavailable);
}

void RecordingSession::serve_error(
  ProxyRequest& request, const std::string& url, StatusCode status_code) {
  if (status_code == StatusCode::unknown)
    status_code = StatusCode::server_error_service_unavailable;
  log(Event::download_failed, url);
  request.send_response(status_code, basic_text_header, {});
}

void RecordingSession::handle_file_request(ProxyRequest request, const std::string& url) {
  if (serve_previously_served(request, url))
    return;

  const auto identifying_url = get_identifying_url(url, request.data());
  const auto entry = m_header_reader.read(identifying_url);
  const auto archived = entry != nullptr;
  if (!archived && m_settings.download_policy == DownloadPolicy::never)
    log(Event::archive_miss, identifying_url);
  const auto cache_info =
    (!archived ? std::nullopt
      : get_cache_info(entry->status_code, entry->header, request.header()));
  const auto expired = (!cache_info.has_value() || cache_info->expired);

  const auto action = get_file_request_action(m_settings, archived, expired);
  if (action.serve && !serve_from_archive(request, url, action.write))
    log(Event::error);

  if (!action.download) {
    if (!request.response_sent())
      serve_error(request, url, StatusCode::server_error_service_unavailable);
    return;
  }
  forward_request(std::move(request), url, cache_info);
}

void RecordingSession::forward_request(
  ProxyRequest request, const std::string& url, const std::optional<CacheInfo>& cache_info) {

  log(Event::download_started, url);

  auto header = request.header();
  strip_proxy_headers(header);
  header.erase("Host");
  header.erase("Content-Length");
  header.emplace("Connection", "close");

  if (cache_info && cache_info->last_modified_time)
    header.emplace("If-Modified-Since", format_time(cache_info->last_modified_time));
  if (cache_info && !cache_info->etag.empty())
    header.emplace("If-None-Match", cache_info->etag);

  auto pending = std::make_shared<ProxyRequest>(std::move(request));
  const auto& timeout = (cache_info ? m_settings.refresh_timeout : m_settings.request_timeout);
  m_client.request(url, pending->method(), std::move(header), pending->data(), timeout,
    [this, url, pending](Client::Response response) {
      handle_response(*pending, url, std::move(response));
    });
}

void RecordingSession::handle_response(
  ProxyRequest& request, const std::string& url, Client::Response response) {

  const auto status_code = response.status_code();
  if (request.method() != "HEAD" && !is_success(status_code) && !is_redirect(status_code))
    if (serve_from_archive(request, url, true))
      return log(Event::download_omitted, url);

  if (response.error()) {
    log(Event::download_failed, response.error_message(), " ", url);
    request.send_response(
      status_code == StatusCode::unknown
        ? StatusCode::server_error_service_unavailable
        : status_code,
      basic_text_header, {});
    return;
  }

  log(Event::download_finished, status_code, " ", response.data().size(), " ", url);
  const auto response_time = std::time(nullptr);

  serve_file(request, url, status_code, response.header(), response.data(), response_time);

  if (request.method() == "HEAD")
    return;

  auto response_lifetime = std::make_shared<Client::Response>(std::move(response));
  const auto& header = response_lifetime->header();
  const auto data = response_lifetime->data();
  const auto target_url = archive_target_url(
    get_identifying_url(url, request.data()), status_code, header);
  if (!target_url)
    return;
  async_write_file(*target_url, status_code, header, data,
    response_time, true,
    [response_lifetime](bool succeeded) {
      if (!succeeded)
        log(Event::writing_failed);
    });
}

bool RecordingSession::serve_previously_served(ProxyRequest& request, const std::string& url) {
  auto lock = std::lock_guard(m_write_mutex);
  const auto identifying_url = get_identifying_url(url, request.data());
  const auto filename = to_local_filename(identifying_url);
  if (m_archive_writer && m_archive_writer->contains(filename))
    if (auto entry = m_header_writer.read(identifying_url)) {
      m_archive_writer->async_read(
        filename, [this, url, entry, request = std::make_shared<ProxyRequest>(std::move(request))](
          ByteVector data, time_t modification_time) mutable {
            serve_file(*request, url, entry->status_code, entry->header, data, modification_time);
        });
      return true;
    }
  return false;
}

bool RecordingSession::serve_from_archive(
  ProxyRequest& request, const std::string& url, bool write_to_archive) {
  if (!m_archive_reader)
    return false;
  const auto identifying_url = get_identifying_url(url, request.data());
  const auto entry = m_header_reader.read(identifying_url);
  if (!entry)
    return false;

  const auto filename = to_local_filename(identifying_url);
  const auto info = m_archive_reader->get_file_info(filename);
  const auto response_time = (info.has_value() ? info->modification_time : std::time(nullptr));
  auto data = m_archive_reader->read(filename);
  serve_file(request, url, entry->status_code, entry->header, data, response_time);
  if (m_settings.download_policy == DownloadPolicy::never)
    log(Event::archive_hit, identifying_url);

  if (write_to_archive) {
    auto data_view = ByteView(data);
    async_write_file(identifying_url, entry->status_code, entry->header, data_view, response_time,
      false, [data = std::move(data)](bool succeeded) {
        if (!succeeded)
          log(Event::writing_failed);
      });
  }
  return true;
}

void RecordingSession::serve_file(ProxyRequest& request, const std::string& url,
  const StatusCode status_code, const Header& header, ByteView data, time_t response_time) {

  if (request.response_sent())
    return;

  auto response_header = header;
  strip_proxy_headers(response_header);
  if (request.method() != "HEAD" && status_code != StatusCode::redirection_not_modified) {
    response_header.erase("Content-Length");
    if (status_code != StatusCode::success_no_content)
      response_header.emplace("Content-Length", std::to_string(data.size()));
  }
  request.send_response(status_code, response_header, data);
  log(Event::served, url);
}

void RecordingSession::async_write_file(const std::string& identifying_url, StatusCode status_code,
  const Header& header, ByteView data, time_t response_time, bool allow_lossy_compression,
  std::function<void(bool)>&& on_complete) {
  auto lock = std::lock_guard(m_write_mutex);
  const auto filename = to_local_filename(identifying_url);
  if (m_archive_writer && !m_archive_writer->contains(filename)) {
    m_header_writer.write(identifying_url, status_code, header);
    if (!data.empty())
      return m_archive_writer->async_write(
        filename, data, response_time, allow_lossy_compression, std::move(on_complete));
  }
  on_complete(true);
}

std::optional<std::string> RecordingSession::archive_target_url(
  const std::string& identifying_url, StatusCode status_code, const Header& header) {
  auto lock = std::lock_guard(m_navigation_mutex);
  const auto canonical = [&](std::string url) {
    auto visited = std::set<std::string>{};
    while (visited.insert(url).second) {
      const auto found = m_redirect_sources.find(url);
      if (found == m_redirect_sources.end())
        break;
      url = found->second;
    }
    return url;
    };

  const auto original_url = canonical(identifying_url);
  if (is_redirect(status_code)) {
    const auto location = header.find("Location");
    if (location != header.end())
      if (const auto target = resolve_http_url(identifying_url, location->second)) {
        m_redirect_sources[*target] = original_url;
        m_pending_documents.insert(original_url);
        return std::nullopt;
      }
  }

  if (!is_success(status_code) && is_document_response(header) &&
    (original_url != identifying_url ||
      same_document_path(identifying_url, m_settings.url))) {
    m_pending_documents.insert(original_url);
    return std::nullopt;
  }

  if (is_success(status_code)) {
    if (original_url != identifying_url)
      return original_url;
    if (is_document_response(header)) {
      if (m_pending_documents.count(m_settings.url) &&
        same_document_path(m_settings.url, identifying_url))
        return m_settings.url;
      for (const auto& pending_url : m_pending_documents)
        if (same_document_path(pending_url, identifying_url))
          return pending_url;
    }
  }
  return identifying_url;
}

void RecordingSession::append_unrequested_files() {
  if (!m_archive_reader || !m_archive_writer)
    return;

  if (m_settings.archive_policy == ArchivePolicy::requested)
    return;

  // set first overlay, so ArchiveReader::Version top and base are always available
  // top is the first and base is the latest archived
  m_archive_reader->set_overlay_path(first_overlay_path);

  for (const auto& [identifying_url, entry] : m_header_reader.entries()) {
    if (m_blocked_hosts && m_blocked_hosts->contains(identifying_url))
      continue;

    const auto filename = to_local_filename(identifying_url);
    auto base_modification_time = m_archive_writer->get_modification_time(filename);

    if (!base_modification_time.has_value()) {
      // write base
      m_header_writer.write(identifying_url, entry.status_code, entry.header);

      const auto version =
        (m_settings.archive_policy == ArchivePolicy::first ? ArchiveReader::top
          : ArchiveReader::base);

      if (const auto info = m_archive_reader->get_file_info(filename, version))
        if (auto data = m_archive_reader->read(filename, version); !data.empty()) {
          const auto data_view = ByteView(data);
          m_archive_writer->async_write(
            filename, data_view, info->modification_time, false, [data = std::move(data)](bool) {});
          base_modification_time = info->modification_time;
        }
    }

    if (m_settings.archive_policy == ArchivePolicy::latest_and_first) {
      // write first
      const auto info = m_archive_reader->get_file_info(filename, ArchiveReader::top);
      if (info && info->modification_time != base_modification_time)
        if (auto data = m_archive_reader->read(filename, ArchiveReader::top); !data.empty()) {
          const auto data_view = ByteView(data);
          m_archive_writer->async_write(first_overlay_path + filename, data_view,
            info->modification_time, false, [data = std::move(data)](bool) {});
        }
    }
  }
}

FileRequestAction get_file_request_action(
  const RecordingSession::Settings& settings, bool archived, bool expired) {

  auto serve = false;
  auto write = false;
  auto download = true;

  if (archived) {
    switch (settings.serve_policy) {
    case ServePolicy::latest:
      if (!expired) {
        serve = true;
        write = true;
        download = false;
      }
      break;

    case ServePolicy::last_archived:
      serve = true;
      if (!expired) {
        write = true;
        download = false;
      }
      break;

    case ServePolicy::first_archived:
      serve = true;
      write = false;
      download = false;
      break;
    }
  }

  if (settings.download_policy == DownloadPolicy::always) {
    if (settings.serve_policy == ServePolicy::latest)
      serve = false;

    write = false;

    if (settings.serve_policy != ServePolicy::first_archived)
      download = true;
  }

  if (settings.download_policy == DownloadPolicy::never) {
    if (archived)
      serve = true;

    if (archived && settings.serve_policy != ServePolicy::first_archived)
      write = true;

    download = false;
  }

  return { serve, write, download };
}

const std::string& RecordingSession::url() const {
  return m_settings.url;
}

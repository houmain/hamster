#include "ControlApi.h"
#include "common.h"
#include "catalog/Catalog.h"
#include "library/Library.h"
#include "recording/RecordingCoordinator.h"
#include "recording/ResourceGraph.h"
#include "proxy/run_proxy.h"
#include "libs/SimpleWeb/utility.hpp"
#include <regex>
#include <set>
#include <utility>
#include <vector>

namespace {
  constexpr auto hamster_version =
#include "_version.h"
    ;

  std::string json(std::string_view value) {
    auto out = std::string("\"");
    constexpr auto hex = "0123456789abcdef";
    for (auto character : value) {
      const auto c = static_cast<unsigned char>(character);
      if (c == '"' || c == '\\') {
        out += '\\';
        out += c;
      }
      else if (c < 32) {
        out += "\\u00";
        out += hex[c >> 4];
        out += hex[c & 15];
      }
      else
        out += c;
    }
    return out + '"';
  }

  const std::string& get(const Header& fields, const std::string& name) {
    if (fields.count(name) != 1)
      throw std::runtime_error("Missing or duplicate field: " + name);
    return fields.find(name)->second;
  }

  ControlResponse json_response(std::string body) {
    return { StatusCode::success_ok, std::move(body),
      {{"Content-Type", "application/json"}} };
  }
} // namespace

ControlApi::ControlApi(
  Library& library, Catalog& catalog, RecordingCoordinator& recordings)
  : m_library(library), m_catalog(catalog), m_recordings(recordings) {}

void ControlApi::set_proxy_port(unsigned short port) {
  m_proxy_port = port;
}

ControlResponse ControlApi::status() {
  return { StatusCode::success_ok, std::to_string(m_proxy_port) };
}

ControlResponse ControlApi::version() {
  return { StatusCode::success_ok, hamster_version };
}

ControlResponse ControlApi::stop() {
  request_proxy_stop();
  return { StatusCode::success_ok, "stopping" };
}

ControlResponse ControlApi::finish_recording(const Header& fields) {
  const auto complete = m_recordings.close_owner(
    ResourceOwner{ get(fields, "path") });
  return { complete ? StatusCode::success_ok : StatusCode::success_accepted,
    complete ? "closed" : "closing" };
}

ControlResponse ControlApi::finish_all_recordings() {
  const auto complete = m_recordings.close_all_owners();
  return { complete ? StatusCode::success_ok : StatusCode::success_accepted,
    complete ? "closed" : "closing" };
}

ControlResponse ControlApi::finish_unlisted_recordings(const Header& fields) {
  const auto count_text = get(fields, "count");
  if (!std::regex_match(count_text, std::regex("0|[1-9][0-9]{0,5}")))
    throw std::runtime_error("Invalid active bookmark count");
  const auto count = std::stoul(count_text);
  auto active_bookmarks = std::set<std::string>{};
  for (auto i = size_t{}; i < count; ++i)
    active_bookmarks.insert(
      get(fields, "path." + std::to_string(i)));
  const auto complete = m_recordings.close_inactive(active_bookmarks);
  return { complete ? StatusCode::success_ok : StatusCode::success_accepted,
    complete ? "closed" : "closing" };
}

ControlResponse ControlApi::set_recording_options(const Header& fields) {
  auto serve = get(fields, "serve");
  if (!iequals_any(serve, "latest", "last", "first"))
    throw std::runtime_error("Invalid recording serve policy");
  auto download = fields.count("download") ? get(fields, "download") : "standard";
  if (!iequals_any(download, "standard", "always", "never"))
    throw std::runtime_error("Invalid recording download policy");
  const auto compression = get(fields, "allow-lossy-compression");
  if (compression != "true" && compression != "false")
    throw std::runtime_error("Invalid recording compression setting");
  m_recordings.set_recording_options(
    std::move(serve), std::move(download), compression == "true");
  return { StatusCode::success_ok, "updated" };
}

ControlResponse ControlApi::replace_bookmarks(const Header& fields) {
  const auto count_text = get(fields, "count");
  if (!std::regex_match(count_text, std::regex("0|[1-9][0-9]{0,5}")))
    throw std::runtime_error("Invalid bookmark catalogue header");
  const auto count = std::stoul(count_text);
  auto bookmarks = std::vector<Bookmark>{};
  bookmarks.reserve(count);
  for (auto i = size_t{}; i < count; ++i) {
    const auto suffix = std::to_string(i);
    auto bookmark = Bookmark{
      get(fields, "url." + suffix),
      get(fields, "path." + suffix) };
    m_library.path(bookmark.path);
    bookmarks.push_back(std::move(bookmark));
  }
  m_catalog.replace_bookmarks(std::move(bookmarks));
  m_recordings.catalog_changed();
  return { StatusCode::success_ok, "synchronized" };
}

ControlResponse ControlApi::update_bookmarks(const Header& fields) {
  const auto removed_count_text = get(fields, "removed-count");
  const auto bookmark_count_text = get(fields, "count");
  const auto count_pattern = std::regex("0|[1-9][0-9]{0,5}");
  if (!std::regex_match(removed_count_text, count_pattern) ||
    !std::regex_match(bookmark_count_text, count_pattern))
    throw std::runtime_error("Invalid bookmark catalogue update header");
  const auto removed_count = std::stoul(removed_count_text);
  const auto bookmark_count = std::stoul(bookmark_count_text);
  auto removed = std::vector<std::string>{};
  removed.reserve(removed_count);
  for (auto i = size_t{}; i < removed_count; ++i) {
    auto bookmark_path = get(
      fields, "removed." + std::to_string(i));
    m_library.path(bookmark_path);
    removed.push_back(std::move(bookmark_path));
  }
  auto bookmarks = std::vector<Bookmark>{};
  bookmarks.reserve(bookmark_count);
  for (auto i = size_t{}; i < bookmark_count; ++i) {
    const auto suffix = std::to_string(i);
    auto bookmark = Bookmark{
      get(fields, "url." + suffix),
      get(fields, "path." + suffix) };
    m_library.path(bookmark.path);
    bookmarks.push_back(std::move(bookmark));
  }
  m_catalog.update_bookmarks(std::move(removed), std::move(bookmarks));
  m_recordings.catalog_changed();
  return { StatusCode::success_ok, "updated" };
}

ControlResponse ControlApi::get_search_index_status() {
  return json_response(std::string("{\"indexing\":") +
    (m_catalog.search_index_busy() ? "true" : "false") + "}");
}

ControlResponse ControlApi::execute_search(const Header& fields) {
  const auto query = get(fields, "query");
  if (query.size() > 4096)
    throw std::runtime_error("Search query too long");
  auto integer = [&](const std::string& name, int fallback, int maximum) {
    if (!fields.count(name))
      return fallback;
    const auto value = get(fields, name);
    if (!std::regex_match(value, std::regex("[0-9]{1,3}")))
      throw std::runtime_error("Invalid search limit");
    const auto number = std::stoi(value);
    if (number < 1 || number > maximum)
      throw std::runtime_error("Invalid search limit");
    return number;
    };
  const auto highlight = fields.count("highlight") &&
    get(fields, "highlight") == "true";
  auto result = std::string("{\"matches\":[");
  auto first = true;
  for (const auto& match : m_catalog.execute_search(query, highlight,
    integer("snippetSize", 16, 64), integer("maxCount", 5, 100))) {
    if (!first)
      result += ',';
    first = false;
    result += "{\"uid\":" + std::to_string(match.uid) +
      ",\"url\":" + json(match.url) +
      ",\"title\":" + json(match.title) +
      ",\"snippet\":" + json(match.snippet) + "}";
  }
  return json_response(result + "],\"indexing\":" +
    (m_catalog.search_index_busy() ? "true" : "false") + "}");
}

ControlResponse ControlApi::update_search_index(const Header& fields) {
  const auto archive = m_library.path(get(fields, "path"));
  if (archive.empty())
    throw std::runtime_error("Empty archive path");
  m_catalog.update_search_index(archive);
  return json_response("{\"indexing\":true}");
}

ControlResponse ControlApi::rebuild_search_index() {
  m_catalog.rebuild_search_index();
  return json_response("{\"indexing\":true}");
}

ControlResponse ControlApi::get_archive_size(const Header& fields) {
  const auto archive = m_library.path(get(fields, "path"));
  if (archive.empty())
    throw std::runtime_error("Empty archive path");
  const auto size = m_catalog.get_archive_size(archive);
  return json_response(
    size ? "{\"fileSize\":" + std::to_string(*size) + "}" : "{}");
}

ControlResponse ControlApi::move_archive(const Header& fields) {
  const auto from = m_library.path(get(fields, "from"));
  const auto to = m_library.path(get(fields, "to"));
  if (from.empty() || to.empty())
    throw std::runtime_error("Empty archive path");
  m_catalog.move_archive(from, to);
  return json_response("{}");
}

ControlResponse ControlApi::delete_archive(const Header& fields) {
  m_catalog.delete_archive(
    get(fields, "path"), get(fields, "id"));
  return json_response("{}");
}

ControlResponse ControlApi::restore_archive(const Header& fields) {
  m_catalog.restore_archive(get(fields, "id"));
  return json_response("{}");
}

ControlResponse ControlApi::get_archive_files(const Header& fields) {
  const auto archive = m_library.path(get(fields, "path"));
  if (archive.empty())
    throw std::runtime_error("Empty archive path");
  auto result = std::string("{\"files\":[");
  auto first = true;
  for (const auto& file : m_catalog.get_archive_files(archive)) {
    if (!first)
      result += ',';
    first = false;
    result += "{\"url\":" + json(file.url) +
      ",\"compressedSize\":" + std::to_string(file.compressed_size) +
      ",\"uncompressedSize\":" + std::to_string(file.uncompressed_size) +
      ",\"modificationTime\":" + std::to_string(file.modification_time) + "}";
  }
  return json_response(result + "]}");
}

ControlResponse ControlApi::get_archive_list() {
  auto result = std::string("{\"files\":[");
  auto first = true;
  for (const auto& archive : m_catalog.get_archive_list()) {
    if (!first)
      result += ',';
    first = false;
    result += "{\"filename\":" + json(archive.filename) +
      ",\"url\":" + json(archive.url) + "}";
  }
  return json_response(result + "]}");
}

ControlResponse ControlApi::handle(const std::string& path, const std::string& content) {
  try {
    const auto bulk_request =
      path == "/replace-bookmarks" || path == "/update-bookmarks" ||
      path == "/finish-unlisted-recordings";
    if (content.size() > (bulk_request ? 8 * 1024 * 1024 : 65536))
      throw std::runtime_error("Control request too large");
    const auto fields = SimpleWeb::QueryString::parse(content);

    if (path == "/status") return status();
    if (path == "/version") return version();
    if (path == "/stop") return stop();
    if (path == "/finish-recording") return finish_recording(fields);
    if (path == "/finish-all-recordings") return finish_all_recordings();
    if (path == "/finish-unlisted-recordings")
      return finish_unlisted_recordings(fields);
    if (path == "/set-recording-options") return set_recording_options(fields);
    if (path == "/replace-bookmarks") return replace_bookmarks(fields);
    if (path == "/update-bookmarks") return update_bookmarks(fields);
    if (path == "/get-search-index-status") return get_search_index_status();
    if (path == "/execute-search") return execute_search(fields);
    if (path == "/update-search-index") return update_search_index(fields);
    if (path == "/rebuild-search-index") return rebuild_search_index();
    if (path == "/get-archive-size") return get_archive_size(fields);
    if (path == "/move-archive") return move_archive(fields);
    if (path == "/delete-archive") return delete_archive(fields);
    if (path == "/restore-archive") return restore_archive(fields);
    if (path == "/get-archive-files") return get_archive_files(fields);
    if (path == "/get-archive-list") return get_archive_list();
    return { StatusCode::client_error_not_found };
  }
  catch (const std::exception& ex) {
    return { StatusCode::client_error_bad_request, ex.what() };
  }
}

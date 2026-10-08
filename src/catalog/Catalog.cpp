#include "Catalog.h"
#include "common.h"
#include "sqlite.h"
#include "library/Archive.h"
#include "library/Indexing.h"
#include "libs/entities/entities.h"
#include "platform.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <set>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace {
  constexpr auto maximum_bookmarks = size_t{ 100000 };

  void validate_path(const std::string& path) {
    if (path.empty() || path.size() > 16384 || path.find('\0') != std::string::npos)
      throw std::invalid_argument("Invalid bookmark archive path");
  }

  void validate_bookmarks(const std::vector<Bookmark>& bookmarks) {
    if (bookmarks.size() > maximum_bookmarks)
      throw std::invalid_argument("Bookmark catalogue is too large");
    auto paths = std::set<std::string>{};
    for (const auto& bookmark : bookmarks) {
      if (bookmark.url.empty() || bookmark.url.size() > 16384 ||
        !iequals_any(get_scheme(bookmark.url), "http", "https"))
        throw std::invalid_argument("Invalid bookmark URL");
      validate_path(bookmark.path);
      if (!paths.insert(bookmark.path).second)
        throw std::invalid_argument("Duplicate bookmark archive path");
    }
  }

  void sort_bookmarks(std::vector<Bookmark>& bookmarks) {
    std::sort(bookmarks.begin(), bookmarks.end(), [](const auto& a, const auto& b) {
      return a.path < b.path;
      });
  }

  std::string_view without_fragment(std::string_view url) {
    const auto fragment = url.find('#');
    return url.substr(0, fragment);
  }

  bool matches_url(std::string_view bookmark_url, std::string_view target_url) {
    bookmark_url = without_fragment(bookmark_url);
    if (target_url == bookmark_url)
      return true;
    if (!starts_with(target_url, bookmark_url))
      return false;
    if (bookmark_url.empty() || bookmark_url.back() == '/')
      return true;
    const auto separator = target_url[bookmark_url.size()];
    return separator == '/' || separator == '?';
  }

  template <typename F>
  auto indexing_task(F&& task) {
    return [task = std::forward<F>(task)]() mutable noexcept {
      try {
        task();
      }
      catch (const std::exception& ex) {
        log(Event::error, "Search indexing: ", ex.what());
      }
      catch (...) {
        log(Event::error, "Unknown indexing failure");
      }
      };
  }

  std::string concatenate(const std::vector<std::string>& texts, std::string_view separator) {
    auto size = size_t{};
    for (const auto& text : texts)
      size += text.size() + separator.size();
    auto total = std::string();
    total.reserve(size);
    for (const auto& text : texts) {
      if (!total.empty() && !text.empty() && !is_punct(text.front()))
        total.insert(end(total), begin(separator), end(separator));
      total.append(text);
    }
    return total;
  }

  std::string decode_html_entities(std::string text) {
    text.resize(decode_html_entities_utf8(text.data(), nullptr));
    return text;
  }

  std::string normalize_space(std::string text) {
    std::replace_if(begin(text), end(text), [](char c) { return is_space(c); }, ' ');
    text.erase(
      std::unique(begin(text), end(text), [](char a, char b) { return a == ' ' && b == ' '; }),
      end(text));
    return text;
  }
}

Catalog::Catalog(const std::filesystem::path& database, Library& library)
  : m_library(library), m_database(std::make_unique<sqlite::Database>()) {
  m_database->open(path_to_utf8(database));
  m_database->set_busy_timeout(5000);
  m_database->execute(R"(
    CREATE TABLE IF NOT EXISTS bookmarks (
      path TEXT PRIMARY KEY NOT NULL,
      url TEXT NOT NULL
    )
  )");
  m_database->execute(R"(
    CREATE VIRTUAL TABLE IF NOT EXISTS pages USING fts5 (
      uid, url, title, text, text_low,
      tokenize = 'unicode61 remove_diacritics 2',
      prefix = '2 3'
    )
  )");
}

Catalog::~Catalog() = default;

std::vector<Bookmark> Catalog::get_bookmarks() const {
  const auto lock = std::lock_guard(m_database_mutex);
  auto result = std::vector<Bookmark>{};
  auto statement = m_database->prepare(
    "SELECT url, path FROM bookmarks ORDER BY path");
  auto rows = statement.query();
  while (rows.step())
    result.push_back(
      { std::string(rows.to_text(0)), std::string(rows.to_text(1)) });
  return result;
}

std::optional<Bookmark> Catalog::find_bookmark(const std::string& path) const {
  validate_path(path);
  const auto lock = std::lock_guard(m_database_mutex);
  auto statement = m_database->prepare(
    "SELECT url, path FROM bookmarks WHERE path = ?");
  statement.bind(0, path);
  auto rows = statement.query();
  if (!rows.step())
    return {};
  return Bookmark{ std::string(rows.to_text(0)), std::string(rows.to_text(1)) };
}

std::vector<Bookmark> Catalog::find_bookmarks_by_url(const std::string& url) const {
  if (url.empty() || url.size() > 16384 || !iequals_any(get_scheme(url), "http", "https"))
    throw std::invalid_argument("Invalid bookmark URL lookup");
  const auto lock = std::lock_guard(m_database_mutex);
  auto result = std::vector<Bookmark>{};
  auto statement = m_database->prepare(R"(
    SELECT url, path
    FROM bookmarks b
    WHERE substr(?, 1, length(CASE WHEN instr(b.url, '#') > 0
      THEN substr(b.url, 1, instr(b.url, '#') - 1) ELSE b.url END)) =
      CASE WHEN instr(b.url, '#') > 0
      THEN substr(b.url, 1, instr(b.url, '#') - 1) ELSE b.url END
    ORDER BY b.path
  )");
  statement.bind(0, url);
  auto rows = statement.query();
  while (rows.step()) {
    auto bookmark = Bookmark{
      std::string(rows.to_text(0)), std::string(rows.to_text(1)) };
    if (matches_url(bookmark.url, url))
      result.push_back(std::move(bookmark));
  }
  return result;
}

bool Catalog::contains_bookmark_for_https_authority(const std::string& authority) const {
  if (authority.empty())
    return false;
  auto normalized = authority;
  if (normalized.size() > 4 && normalized.compare(normalized.size() - 4, 4, ":443") == 0)
    normalized.resize(normalized.size() - 4);
  const auto lock = std::lock_guard(m_database_mutex);
  auto statement = m_database->prepare(
    "SELECT url FROM bookmarks WHERE lower(url) LIKE lower(?)");
  statement.bind(0, "https://" + normalized + "%");
  auto rows = statement.query();
  while (rows.step()) {
    const auto url = std::string(rows.to_text(0));
    auto bookmark_authority = std::string(get_hostname_port(url));
    if (bookmark_authority.size() > 4 &&
      bookmark_authority.compare(bookmark_authority.size() - 4, 4, ":443") == 0)
      bookmark_authority.resize(bookmark_authority.size() - 4);
    if (iequals(bookmark_authority, normalized))
      return true;
  }
  return false;
}

void Catalog::replace_bookmarks(std::vector<Bookmark> bookmarks) {
  validate_bookmarks(bookmarks);
  sort_bookmarks(bookmarks);

  const auto lock = std::lock_guard(m_database_mutex);
  m_database->execute("BEGIN IMMEDIATE");
  try {
    m_database->execute("DELETE FROM bookmarks");

    auto insert = m_database->prepare(
      "INSERT INTO bookmarks (url, path) VALUES (?, ?)");
    for (const auto& bookmark : bookmarks) {
      insert.bind(0, bookmark.url);
      insert.bind(1, bookmark.path);
      insert.execute();
    }
    m_database->execute("COMMIT");
  }
  catch (...) {
    m_database->execute("ROLLBACK");
    throw;
  }
}

void Catalog::update_bookmarks(
  std::vector<std::string> removed_paths, std::vector<Bookmark> bookmarks) {
  if (removed_paths.size() > maximum_bookmarks)
    throw std::invalid_argument("Bookmark catalogue update is too large");
  for (const auto& path : removed_paths)
    validate_path(path);
  validate_bookmarks(bookmarks);

  const auto lock = std::lock_guard(m_database_mutex);
  m_database->execute("BEGIN IMMEDIATE");
  try {
    auto remove = m_database->prepare("DELETE FROM bookmarks WHERE path = ?");
    for (const auto& path : removed_paths) {
      remove.bind(0, path);
      remove.execute();
    }
    auto upsert = m_database->prepare(R"(
      INSERT INTO bookmarks (url, path) VALUES (?, ?)
      ON CONFLICT(path) DO UPDATE SET url = excluded.url
    )");
    for (const auto& bookmark : bookmarks) {
      upsert.bind(0, bookmark.url);
      upsert.bind(1, bookmark.path);
      upsert.execute();
    }
    m_database->execute("COMMIT");
  }
  catch (...) {
    m_database->execute("ROLLBACK");
    throw;
  }
}

void Catalog::queue_search_index_update(const std::filesystem::path& archive) {
  // Parse an immutable snapshot so moving or rerecording an archive cannot race
  // with background indexing, including Windows file sharing.
  struct Snapshot {
    std::filesystem::path path;
    ~Snapshot() {
      auto ec = std::error_code{};
      std::filesystem::remove(path, ec);
    }
  };

  auto snapshot = std::make_shared<Snapshot>();
  snapshot->path = generate_temporary_filename("hamster-index-");
  std::filesystem::copy_file(archive, snapshot->path);
  m_worker.execute(
    indexing_task([this, snapshot] { write_search_index(snapshot->path); }));
}

void Catalog::write_search_index(const std::filesystem::path& archive) {
  auto reader = ArchiveReader{};
  if (!reader.open(archive))
    throw std::runtime_error("Indexing archive failed");

  struct Page {
    std::string url;
    std::string title;
    std::string text;
    std::string low;
  };

  auto pages = std::vector<Page>{};
  auto total = size_t{};
  const auto uid = get_archive_uid(reader);
  for_each_archive_html(reader, [&](ArchiveHtml html) {
    auto title = std::string{};
    auto text = std::vector<std::string>{};
    auto low = std::vector<std::string>{};
    if (html.plaintext) {
      title = html.url;
      text.emplace_back(html.html);
    }
    else
      for_each_html_text(html.html, [&](std::string_view value, HtmlSection section) {
      if (section == HtmlSection::title)
        title += value;
      else if (section == HtmlSection::navigation)
        low.emplace_back(value);
      else
        text.emplace_back(value);
        });
    if (title.empty())
      title = html.url;
    auto clean = [&](std::string value) {
      return normalize_space(
        html.plaintext ? std::move(value) : decode_html_entities(std::move(value)));
      };
    auto page = Page{ html.url, clean(std::move(title)), clean(concatenate(text, " ")),
      clean(concatenate(low, " | ")) };
    total += page.title.size() + page.text.size() + page.low.size();
    if (total > 128 * 1024 * 1024)
      throw std::runtime_error("Archive exceeds indexing text limit");
    if (!page.text.empty() || !page.low.empty())
      pages.push_back(std::move(page));
    });

  // Parsing is deliberately outside the database lock. Only the replacement is atomic.
  const auto lock = std::lock_guard(m_database_mutex);
  m_database->execute("BEGIN IMMEDIATE");
  try {
    auto clear = m_database->prepare("DELETE FROM pages WHERE uid = ?");
    clear.bind(0, uid);
    clear.execute();
    auto insert =
      m_database->prepare("INSERT INTO pages (uid,url,title,text,text_low) VALUES (?,?,?,?,?)");
    for (const auto& page : pages) {
      insert.bind(0, uid);
      insert.bind(1, page.url);
      insert.bind(2, page.title);
      insert.bind(3, page.text);
      insert.bind(4, page.low);
      insert.execute();
    }
    m_database->execute("COMMIT");
  }
  catch (...) {
    m_database->execute("ROLLBACK");
    throw;
  }
}

void Catalog::clear_search_index() {
  const auto lock = std::lock_guard(m_database_mutex);
  m_database->execute("DELETE FROM pages");
}

void Catalog::remove_archive_from_search_index(int64_t uid) {
  const auto lock = std::lock_guard(m_database_mutex);
  auto statement = m_database->prepare("DELETE FROM pages WHERE uid = ?");
  statement.bind(0, uid);
  statement.execute();
}

std::vector<SearchResult> Catalog::execute_search(
  std::string_view query, bool highlight, int snippet_size, int max_count) {
  const auto lock = std::lock_guard(m_database_mutex);
  auto added = std::unordered_set<std::string>{};
  auto matches = std::vector<SearchResult>{};
  for (auto [column_index, column_name] : {
         std::pair{3, "text"},
         std::pair{4, "text_low"},
    }) {
    if (max_count <= 0)
      break;
    const auto format = R"(
      SELECT uid, url, title, snippet(pages, %i, %s, %s, '', %i)
      FROM pages
      WHERE %s MATCH ?
      ORDER BY RANK
      LIMIT %i
    )";
    auto buffer = std::array<char, 256>{};
    std::snprintf(buffer.data(), buffer.size(), format, column_index,
      highlight ? "'<b>'" : "''", highlight ? "'</b>'" : "''",
      snippet_size, column_name, max_count);
    auto select = m_database->prepare(buffer.data());
    select.bind(0, query);
    auto result = select.query();
    while (result.step()) {
      const auto uid = result.to_int64(0);
      const auto url = result.to_text(1);
      const auto title = result.to_text(2);
      const auto snippet = result.to_text(3);
      if (added.insert(std::string(url)).second) {
        matches.push_back({ uid, std::string(url), std::string(title), std::string(snippet) });
        --max_count;
      }
    }
  }
  return matches;
}

bool Catalog::search_index_busy() const {
  return m_worker.busy();
}

void Catalog::update_search_index(const std::filesystem::path& archive_or_directory) {
  m_library.idle(archive_or_directory);
  m_library.for_archives(archive_or_directory,
    [&](const auto& archive, int64_t) { queue_search_index_update(archive); });
}

void Catalog::rebuild_search_index() {
  m_library.idle(m_library.root());
  m_worker.execute(indexing_task([this] { clear_search_index(); }));
  m_library.for_archives(
    m_library.root(),
    [&](const auto& archive, int64_t) { queue_search_index_update(archive); });
}

std::optional<uintmax_t> Catalog::get_archive_size(
  const std::filesystem::path& archive) const {
  return m_library.size(archive);
}

void Catalog::move_archive(
  const std::filesystem::path& from, const std::filesystem::path& to) {
  m_library.move(from, to);
}

void Catalog::delete_archive(const std::string& name, const std::string& undo_id) {
  const auto removed = m_library.remove(name, undo_id);
  m_worker.execute(indexing_task([this, removed] {
    for (auto uid : removed)
      remove_archive_from_search_index(uid);
    }));
}

void Catalog::restore_archive(const std::string& undo_id) {
  if (const auto restored = m_library.restore(undo_id))
    update_search_index(*restored);
}

std::vector<ArchiveFile> Catalog::get_archive_files(
  const std::filesystem::path& archive) const {
  return m_library.files(archive);
}

std::vector<LibraryArchive> Catalog::get_archive_list() const {
  return m_library.list();
}

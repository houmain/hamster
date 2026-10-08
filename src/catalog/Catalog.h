#pragma once

#include "BackgroundWorker.h"
#include "library/Library.h"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sqlite {
  class Database;
}

struct Bookmark {
  std::string url;
  std::string path;

  bool operator==(const Bookmark& rhs) const {
    return url == rhs.url && path == rhs.path;
  }
};

struct SearchResult {
  int64_t uid;
  std::string url;
  std::string title;
  std::string snippet;
};

// Persistent metadata and search index for the archives in a Library.
class Catalog final {
public:
  Catalog(const std::filesystem::path& database, Library& library);
  ~Catalog();

  void replace_bookmarks(std::vector<Bookmark> bookmarks);
  void update_bookmarks(
    std::vector<std::string> removed_paths, std::vector<Bookmark> bookmarks);
  std::vector<Bookmark> get_bookmarks() const;
  std::optional<Bookmark> find_bookmark(const std::string& path) const;
  std::vector<Bookmark> find_bookmarks_by_url(const std::string& url) const;
  bool contains_bookmark_for_https_authority(const std::string& authority) const;

  bool search_index_busy() const;
  std::vector<SearchResult> execute_search(
    std::string_view query, bool highlight, int snippet_size, int max_count);
  void update_search_index(const std::filesystem::path& archive_or_directory);
  void rebuild_search_index();

  std::optional<uintmax_t> get_archive_size(const std::filesystem::path& archive) const;
  void move_archive(
    const std::filesystem::path& from, const std::filesystem::path& to);
  void delete_archive(const std::string& name, const std::string& undo_id);
  void restore_archive(const std::string& undo_id);
  std::vector<ArchiveFile> get_archive_files(
    const std::filesystem::path& archive) const;
  std::vector<LibraryArchive> get_archive_list() const;

private:
  void queue_search_index_update(const std::filesystem::path& archive);
  void write_search_index(const std::filesystem::path& archive);
  void clear_search_index();
  void remove_archive_from_search_index(int64_t uid);

  Library& m_library;
  mutable std::mutex m_database_mutex;
  std::unique_ptr<sqlite::Database> m_database;
  // Destroy the worker first so queued tasks finish while the database is alive.
  BackgroundWorker m_worker;
};

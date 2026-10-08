#pragma once

#include "Indexing.h"
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

struct LibraryArchive {
  std::string filename;
  std::string url;
};

class Library {
public:
  explicit Library(std::filesystem::path directory);

  void opened(const std::string& id, const std::filesystem::path& input,
    const std::filesystem::path& output);
  std::filesystem::path committed(const std::string& id);
  std::vector<std::filesystem::path> committed_all();

  const std::filesystem::path& root() const;
  std::filesystem::path path(const std::string& name, bool internal = false) const;
  void for_archives(const std::filesystem::path& path,
    const std::function<void(const std::filesystem::path&, int64_t)>& action) const;
  void idle(const std::filesystem::path& path) const;
  void move(const std::filesystem::path& from, const std::filesystem::path& to);
  std::vector<int64_t> remove(const std::string& name, const std::string& undo_id);
  std::optional<std::filesystem::path> restore(const std::string& undo_id);
  std::optional<uintmax_t> size(const std::filesystem::path& path) const;
  std::vector<ArchiveFile> files(const std::filesystem::path& path) const;
  std::vector<LibraryArchive> list() const;

private:
  static void validate_undo_id(const std::string& id);

  std::filesystem::path m_root;
  std::map<std::string, std::pair<std::filesystem::path, std::filesystem::path>> m_active;
};

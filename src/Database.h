#pragma once

#include <memory>
#include <filesystem>
#include <functional>

namespace sqlite { class Database; }

struct SearchResult {
  int64_t uid;
  std::string_view url;
  std::string_view snippet;
};

class Database {
public:
  explicit Database(const std::filesystem::path& path);
  ~Database();

  void update_index(const std::filesystem::path& path);
  void execute_search(std::string_view query,
    bool highlight, int snippet_size, int max_count,
    const std::function<void(SearchResult)>& match_callback);

private:
  std::unique_ptr<sqlite::Database> m_db;
};

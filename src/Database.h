#pragma once

#include <memory>
#include <filesystem>

namespace sqlite { class Database; }

class Database {
public:
  explicit Database(const std::filesystem::path& path);
  ~Database();

  void update_index(const std::filesystem::path& path);

private:
  std::unique_ptr<sqlite::Database> m_db;
};

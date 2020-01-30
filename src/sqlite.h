#pragma once

#include "libs/nonstd/span.hpp"
#include <stdexcept>
#include <string>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace sqlite {

enum class Type {
  Integer = 1,
  Double = 2,
  Text = 3,
  Blob = 4,
  Null = 5,
};

struct Exception : std::runtime_error {
  using std::runtime_error::runtime_error;
};

//-------------------------------------------------------------------------

class QueryResult {
public:
  QueryResult(QueryResult&& rhs);
  QueryResult& operator=(QueryResult&& rhs);
  ~QueryResult();
  bool step();
  Type type(int column) const;
  bool is_null(int column);
  int to_int(int column);
  int64_t to_int64(int column);
  double to_double(int column);
  std::string_view to_text(int column);
  nonstd::span<const std::byte> to_blob(int column);

private:
  friend class Statement;
  explicit QueryResult(sqlite3_stmt* statement);

  sqlite3_stmt* m_statement{ };
};

//-------------------------------------------------------------------------

class Statement {
public:
  Statement() = default;
  Statement(Statement&& rhs);
  Statement& operator=(Statement&& rhs);
  ~Statement();
  int column_count() const { return m_columns; }
  std::string_view column_name(int column) const;
  std::string_view column_table_name(int column) const;
  std::string_view column_origin_name(int column) const;
  std::string_view column_type_declaration(int column) const;
  void bind_null(int parameter);
  void bind(int parameter, int value);
  void bind(int parameter, int64_t value);
  void bind(int parameter, double value);
  void bind(int parameter, std::string_view string);
  void bind(int parameter, nonstd::span<const std::byte> blob);
  int execute();
  QueryResult query();

private:
  friend class Database;
  explicit Statement(sqlite3_stmt* statement);

  sqlite3_stmt* m_statement{ };
  int m_columns{ };
};

//-------------------------------------------------------------------------

class Database {
public:
  Database() = default;
  Database(Database&& rhs) noexcept;
  Database& operator=(Database&& rhs) noexcept;
  ~Database();

  void open(const std::string& filename);
  void close();
  int execute(std::string_view sql);
  Statement prepare(std::string_view sql);
  int64_t last_insert_rowid();
  void interrupt();

private:
  sqlite3* m_database{ };
};

} // namespace

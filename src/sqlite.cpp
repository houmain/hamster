
#include "sqlite.h"
#include <sqlite3.h>

namespace sqlite {

namespace {
  [[noreturn]] void error(const sqlite3* database) {
    throw Exception(sqlite3_errmsg(const_cast<sqlite3*>(database)));
  }
} // namespace

//-------------------------------------------------------------------------

QueryResult::QueryResult(sqlite3_stmt* statement)
  : m_statement(statement) {
}

QueryResult::QueryResult(QueryResult&& rhs)
  : m_statement(std::exchange(rhs.m_statement, nullptr)) {
}

QueryResult& QueryResult::operator=(QueryResult&& rhs) {
  auto tmp = std::move(rhs);
  std::swap(m_statement, tmp.m_statement);
  return *this;
}

QueryResult::~QueryResult() {
  if (m_statement)
    sqlite3_reset(m_statement);
}

bool QueryResult::step() {
  const auto result = sqlite3_step(m_statement);
  if (result == SQLITE_DONE)
    return false;
  if (result != SQLITE_ROW)
    error(sqlite3_db_handle(m_statement));
  return true;
}

Type QueryResult::type(int column) const {
  return static_cast<Type>(sqlite3_column_type(m_statement, column));
}

bool QueryResult::is_null(int column) {
  return (sqlite3_column_type(m_statement, column) == SQLITE_NULL);
}

int QueryResult::to_int(int column) {
  return sqlite3_column_int(m_statement, column);
}

int64_t QueryResult::to_int64(int column) {
  return sqlite3_column_int64(m_statement, column);
}

double QueryResult::to_double(int column) {
  return sqlite3_column_double(m_statement, column);
}

std::string_view QueryResult::to_text(int column) {
  const auto str = sqlite3_column_text(m_statement, column);
  const auto length = sqlite3_column_bytes(m_statement, column);
  return { reinterpret_cast<const char*>(str), static_cast<size_t>(length) };
}

nonstd::span<const std::byte> QueryResult::to_blob(int column) {
  const auto data = sqlite3_column_blob(m_statement, column);
  const auto length = sqlite3_column_bytes(m_statement, column);
  return { static_cast<const std::byte*>(data), length };
}

//-------------------------------------------------------------------------

Statement::Statement(sqlite3_stmt* statement)
  : m_statement(statement),
    m_columns(sqlite3_column_count(m_statement)) {
}

Statement::Statement(Statement&& rhs)
  : m_statement(std::exchange(rhs.m_statement, nullptr)),
    m_columns(rhs.m_columns) {
}

Statement& Statement::operator=(Statement&& rhs) {
  auto tmp = std::move(rhs);
  std::swap(m_statement, tmp.m_statement);
  std::swap(m_columns, tmp.m_columns);
  return *this;
}

Statement::~Statement() {
  sqlite3_finalize(m_statement);
}

std::string_view Statement::column_name(int column) const {
  return sqlite3_column_name(m_statement, column);
}

#if defined(SQLITE_ENABLE_COLUMN_METADATA)
std::string_view Statement::column_table_name(int column) const {
  return sqlite3_column_table_name(m_statement, column);
}

std::string_view Statement::column_origin_name(int column) const {
  return sqlite3_column_origin_name(m_statement, column);
}
#endif // SQLITE_ENABLE_COLUMN_METADATA

std::string_view Statement::column_type_declaration(int column) const {
  return sqlite3_column_decltype(m_statement, column);
}

void Statement::bind_null(int parameter) {
  if (sqlite3_bind_null(m_statement, parameter + 1))
    error(sqlite3_db_handle(m_statement));
}

void Statement::bind(int parameter, int value) {
  if (sqlite3_bind_int(m_statement, parameter + 1, value))
    error(sqlite3_db_handle(m_statement));
}

void Statement::bind(int parameter, int64_t value) {
  if (sqlite3_bind_int64(m_statement, parameter + 1, value))
    error(sqlite3_db_handle(m_statement));
}

void Statement::bind(int parameter, double value) {
  if (sqlite3_bind_double(m_statement, parameter + 1, value))
    error(sqlite3_db_handle(m_statement));
}

void Statement::bind(int parameter, std::string_view string) {
  if (sqlite3_bind_text(m_statement, parameter + 1,
      string.data(), static_cast<int>(string.size()), SQLITE_TRANSIENT))
    error(sqlite3_db_handle(m_statement));
}

void Statement::bind(int parameter, nonstd::span<const std::byte> blob) {
  if (sqlite3_bind_blob(m_statement, parameter + 1,
      blob.data(), static_cast<int>(blob.size()), SQLITE_TRANSIENT))
    error(sqlite3_db_handle(m_statement));
}

int Statement::execute() {
  if (!m_statement)
    return 0;

  if (sqlite3_stmt_busy(m_statement))
    error(sqlite3_db_handle(m_statement));

  const auto result = sqlite3_step(m_statement);
  if (result != SQLITE_DONE)
    error(sqlite3_db_handle(m_statement));

  const auto changes = sqlite3_changes(sqlite3_db_handle(m_statement));
  sqlite3_reset(m_statement);
  return changes;
}

QueryResult Statement::query() {
  if (sqlite3_stmt_busy(m_statement))
    error(sqlite3_db_handle(m_statement));
  return QueryResult{ m_statement };
}

//-------------------------------------------------------------------------

Database::Database(Database&& rhs) noexcept
  : m_database(std::exchange(rhs.m_database, nullptr)) {
}

Database& Database::operator=(Database&& rhs) noexcept {
  auto tmp = std::move(rhs);
  std::swap(tmp.m_database, m_database);
  return *this;
}

Database::~Database() {
  close();
}

void Database::open(const std::string& filename) {
  close();
  if (sqlite3_open(filename.c_str(), &m_database))
    error(m_database);
}

void Database::close() {
  sqlite3_close(std::exchange(m_database, nullptr));
}

int Database::execute(std::string_view sql) {
  return prepare(sql).execute();
}

Statement Database::prepare(std::string_view sql) {
  auto statement = std::add_pointer_t<sqlite3_stmt>{ };
  if (sqlite3_prepare_v2(m_database,
      sql.data(), static_cast<int>(sql.size()),
      &statement, nullptr))
    error(m_database);
  return Statement{ statement };
}

void Database::interrupt() {
  sqlite3_interrupt(m_database);
}

int64_t Database::last_insert_rowid() {
  return sqlite3_last_insert_rowid(m_database);
}

} // namespace

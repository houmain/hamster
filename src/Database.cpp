
#include "Database.h"
#include "sqlite.h"
#include "Indexing.h"
#include <algorithm>

namespace {
  void normalize_space(std::string& text) {
    std::replace_if(begin(text), end(text),
      [](char c) { return std::isspace(c); }, ' ');
    text.erase(std::unique(begin(text), end(text),
      [](char a, char b) { return (a == ' ' && a == b); }), end(text));
  }
} // namespace

Database::Database(const std::filesystem::path& path)
  : m_db(new sqlite::Database()) {
  m_db->open(path.u8string());
  m_db->execute(R"(
    CREATE VIRTUAL TABLE IF NOT EXISTS texts USING fts5 (
      uid, url, text,
      tokenize=porter
    )
  )");
}

Database::~Database() = default;

void Database::update_index(const std::filesystem::path& filename) {
  auto clear = m_db->prepare(R"(
    DELETE FROM texts WHERE uid = ?
  )");
  auto insert = m_db->prepare(R"(
    INSERT INTO texts
      (uid, url, text)
    VALUES
      (?, ?, ?)
  )");

  auto deleted = false;
  for_each_archive_html(filename,
    [&](ArchiveHtml html) {
      if (!std::exchange(deleted, true)) {
        clear.bind(1, html.uid);
        clear.execute();
      }
      auto texts = std::vector<std::string_view>();
      auto size = size_t{ };
      for_html_text(html.html,
        [&](std::string_view text) {
          texts.push_back(text);
          size += text.size() + 1;
        });
      if (!size)
        return;
      auto total_text = std::string();
      total_text.reserve(size);
      for (const auto& text : texts) {
        if (!total_text.empty() &&
            !text.empty() &&
            !std::ispunct(text.front()))
          total_text.push_back(' ');
        total_text.append(text);
      }
      normalize_space(total_text);
      insert.bind(1, html.uid);
      insert.bind(2, html.url);
      insert.bind(3, total_text);
      insert.execute();
    });
}

void Database::execute_search(std::string_view query,
    const std::function<void(SearchResult)>& match_callback) {
  auto select = m_db->prepare(R"(
    SELECT uid, url, snippet(texts, 2, '<b>', '</b>', '...', 32)
    FROM texts
    WHERE text MATCH ?
    ORDER BY bm25(texts)
  )");
  select.bind(1, query);
  auto result = select.query();
  while (result.step()) {
    const auto uid = result.to_int64(0);
    const auto url = result.to_text(1);
    const auto snippet = result.to_text(2);
    match_callback({ uid, url, snippet });
  }
}


#include "Database.h"
#include "sqlite.h"
#include "Indexing.h"
#include "libs/entities/entities.h"
#include <algorithm>

namespace {
  void normalize_space(std::string& text) {
    std::replace_if(begin(text), end(text),
      [](unsigned char c) { return std::isspace(c); }, ' ');
    text.erase(std::unique(begin(text), end(text),
      [](char a, char b) { return (a == ' ' && a == b); }), end(text));
  }

  std::string concatenate(std::vector<std::string_view> texts) {
    auto size = size_t{ };
    for (const auto& text : texts)
      size += text.size() + 1;
    auto total = std::string();
    total.reserve(size);
    for (const auto& text : texts) {
      if (!total.empty() &&
          !text.empty() &&
          !std::ispunct(text.front()))
        total.push_back(' ');
      total.append(text);
    }
    total.resize(decode_html_entities_utf8(total.data(), nullptr));
    normalize_space(total);
    return total;
  }
} // namespace

Database::Database(const std::filesystem::path& path)
  : m_db(new sqlite::Database()) {
  m_db->open(path.u8string());
  m_db->execute(R"(
    CREATE VIRTUAL TABLE IF NOT EXISTS texts USING fts5 (
      uid, url, text, text_low,
      tokenize = 'unicode61 remove_diacritics 2',
      prefix = '2 3'
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
      (uid, url, text, text_low)
    VALUES
      (?, ?, ?, ?)
  )");

  auto deleted = false;
  for_each_archive_html(filename,
    [&](ArchiveHtml html) {
      if (!std::exchange(deleted, true)) {
        clear.bind(0, html.uid);
        clear.execute();
      }
      auto text = std::vector<std::string_view>();
      auto text_low = std::vector<std::string_view>();
      for_each_html_text(html.html,
        [&](std::string_view string, bool low_priority) {
          if (!low_priority)
            text.push_back(string);
          else
            text_low.push_back(string);
        });
      if (text.empty() && text_low.empty())
        return;

      insert.bind(0, html.uid);
      insert.bind(1, html.url);
      insert.bind(2, concatenate(text));
      insert.bind(3, concatenate(text_low));
      insert.execute();
    });
}

void Database::execute_search(std::string_view query,
    bool highlight, int snippet_size, int max_count,
    const std::function<void(SearchResult)>& match_callback) {
  for (auto [column_index, column_name] : {
      std::pair{ 2, "text" },
      std::pair{ 3, "text_low" },
    }) {

    if (max_count <= 0)
      return;

    const auto format = R"(
      SELECT uid, url, snippet(texts, %i, %s, %s, '...', %i)
      FROM texts
      WHERE %s MATCH ?
      ORDER BY RANK
      LIMIT %i
    )";
    auto buffer = std::array<char, 256>();
    std::snprintf(buffer.data(), buffer.size(), format,
      column_index,
      highlight ? "'<b>'" : "''",
      highlight ? "'</b>'" : "''",
      snippet_size,
      column_name,
      max_count);
    auto select = m_db->prepare(buffer.data());
    select.bind(0, query);
    auto result = select.query();
    while (result.step()) {
      const auto uid = result.to_int64(0);
      const auto url = result.to_text(1);
      const auto snippet = result.to_text(2);
      match_callback({ uid, url, snippet });
      --max_count;
    }
  }
}

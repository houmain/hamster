
#include "Database.h"
#include "sqlite.h"
#include "Indexing.h"
#include "libs/entities/entities.h"
#include <algorithm>
#include <unordered_set>

namespace {
  std::string concatenate(std::vector<std::string_view> texts, std::string_view separator) {
    auto size = size_t{ };
    for (const auto& text : texts)
      size += text.size() + separator.size();
    auto total = std::string();
    total.reserve(size);
    for (const auto& text : texts) {
      if (!total.empty() &&
          !text.empty() &&
          !is_punct(text.front()))
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
    std::replace_if(begin(text), end(text),
      [](char c) { return is_space(c); }, ' ');
    text.erase(std::unique(begin(text), end(text),
      [](char a, char b) { return (a == ' ' && b == ' '); }), end(text));
    return text;
  }
} // namespace

Database::Database(const std::filesystem::path& path)
  : m_db(new sqlite::Database()) {
  m_db->open(path_to_utf8(path));
  m_db->execute(R"(
    CREATE VIRTUAL TABLE IF NOT EXISTS pages USING fts5 (
      uid, url, title, text, text_low,
      tokenize = 'unicode61 remove_diacritics 2',
      prefix = '2 3'
    )
  )");
}

Database::~Database() = default;

void Database::update_index(const std::filesystem::path& filename) {
  auto clear = m_db->prepare(R"(
    DELETE FROM pages WHERE uid = ?
  )");
  auto insert = m_db->prepare(R"(
    INSERT INTO pages
      (uid, url, title, text, text_low)
    VALUES
      (?, ?, ?, ?, ?)
  )");

  const auto uid = get_archive_uid(filename);

  auto deleted = false;
  for_each_archive_html(filename,
    [&](ArchiveHtml html) {
      if (!std::exchange(deleted, true)) {
        clear.bind(0, uid);
        clear.execute();
      }
      auto title = std::string_view();
      auto text = std::vector<std::string_view>();
      auto text_low = std::vector<std::string_view>();
      for_each_html_text(html.html,
        [&](std::string_view string, HtmlSection section) {
          switch (section) {
            case HtmlSection::heading:
            case HtmlSection::content:
              text.push_back(string);
              break;
            case HtmlSection::navigation:
              text_low.push_back(string);
              break;
            case HtmlSection::title:
              title = string;
              break;
          }
        });
      if (!title.empty() && (!text.empty() || !text_low.empty())) {
        insert.bind(0, uid);
        insert.bind(1, html.url);
        insert.bind(2, normalize_space(decode_html_entities(std::string(title))));
        insert.bind(3, normalize_space(decode_html_entities(concatenate(text, " "))));
        insert.bind(4, normalize_space(decode_html_entities(concatenate(text_low, " | "))));
        insert.execute();
      }
    });
}

void Database::execute_search(std::string_view query,
    bool highlight, int snippet_size, int max_count,
    const std::function<void(SearchResult)>& match_callback) {

  auto added = std::unordered_set<std::string_view>();
  for (auto [column_index, column_name] : {
      std::pair{ 3, "text" },
      std::pair{ 4, "text_low" },
    }) {

    if (max_count <= 0)
      return;

    const auto format = R"(
      SELECT uid, url, title, snippet(pages, %i, %s, %s, '', %i)
      FROM pages
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
      const auto title = result.to_text(2);
      const auto snippet = result.to_text(3);
      if (!added.count(url)) {
        added.insert(url);
        match_callback({ uid, url, title, snippet });
        --max_count;
      }
    }
  }
}

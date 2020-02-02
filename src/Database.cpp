
#include "Database.h"
#include "sqlite.h"
#include "Indexing.h"

Database::Database(const std::filesystem::path& path)
  : m_db(new sqlite::Database()) {
  m_db->open(path.u8string());
  m_db->execute(R"(
    CREATE VIRTUAL TABLE IF NOT EXISTS "texts" USING fts4 (
      "uid" INTEGER NOT NULL,
      "filename" TEXT NOT NULL,
      "text" TEXT NOT NULL,
      tokenize=porter)
    )");
}

Database::~Database() = default;

void Database::update_index(const std::filesystem::path& filename) {
  auto clear = m_db->prepare(R"(
    DELETE FROM "texts" WHERE "uid" = ?
  )");
  auto insert = m_db->prepare(R"(
    INSERT INTO "texts"
      ("uid", "filename", "text")
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
      insert.bind(1, html.uid);
      insert.bind(2, html.filename);
      insert.bind(3, total_text);
      insert.execute();
    });
}


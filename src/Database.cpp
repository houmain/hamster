
#include "Database.h"
#include "sqlite.h"
#include "Indexing.h"

Database::Database(const std::filesystem::path& path)
  : m_db(new sqlite::Database()) {
  m_db->open(path.u8string());
  m_db->execute(R"(
    CREATE VIRTUAL TABLE IF NOT EXISTS "texts" USING fts4 (
      "uid"    INTEGER NOT NULL,
      "offset" INTEGER NOT NULL,
      "text",
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
      ("uid", "offset", "text")
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
      for_html_text(html.html,
        [&](std::string_view text) {
          const auto offset = std::distance(html.html.data(), text.data());
          insert.bind(1, html.uid);
          insert.bind(2, offset);
          insert.bind(3, text);
          insert.execute();
        });
    });
}


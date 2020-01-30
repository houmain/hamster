#pragma once

#include "common.h"
#include <filesystem>
#include <functional>

struct ArchiveHtml {
  int64_t uid;
  std::string filename;
  std::string_view html;
};

bool for_each_archive_html(const std::filesystem::path& filename,
  std::function<void(ArchiveHtml)> file_callback);
void for_html_text(std::string_view html,
  std::function<void(std::string_view)> text_callback);

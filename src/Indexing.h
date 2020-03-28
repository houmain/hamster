#pragma once

#include "common.h"
#include <filesystem>
#include <functional>

struct ArchiveHtml {
  int64_t uid;
  std::string url;
  std::string_view html;
};

enum class HtmlSection {
  content,
  heading,
  title,
  navigation,
};

bool for_each_archive_html(const std::filesystem::path& filename,
  std::function<void(ArchiveHtml)> file_callback);
void for_each_html_text(std::string_view html,
  std::function<void(std::string_view, HtmlSection)> text_callback);

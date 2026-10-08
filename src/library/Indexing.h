#pragma once

#include "common.h"
#include "Archive.h"
#include <filesystem>
#include <functional>

struct ArchiveFile {
  std::string url;
  uint64_t compressed_size;
  uint64_t uncompressed_size;
  time_t modification_time;
};

struct ArchiveHtml {
  std::string url;
  std::string_view html;
  bool plaintext{};
};

enum class HtmlSection {
  content,
  heading,
  title,
  navigation,
};

int64_t get_archive_uid(const ArchiveReader& reader);
void for_each_archive_file(const ArchiveReader& reader,
  std::function<void(ArchiveFile)> file_callback);
void for_each_archive_html(const ArchiveReader& reader,
  std::function<void(ArchiveHtml)> file_callback);
void for_each_html_text(std::string_view html,
  std::function<void(std::string_view, HtmlSection)> text_callback);
void for_each_html_resource(std::string_view document_url, std::string_view html,
  std::function<void(std::string)> resource_callback);
void for_each_css_resources(std::string_view stylesheet_url, std::string_view css,
  std::function<void(std::string)> resource_callback);


#include "Indexing.h"
#include "gumbo.h"
#include "libs/webrecorder/src/HeaderStore.h"
#include <cstring>
#include <sstream>

namespace {
  bool is_html_or_plaintext_mime_type(std::string_view mime_type) {
    return iequals_any(mime_type,
      "text/html", "text/plain", "text", "html", "plain");
  }
} // namespace

int64_t get_archive_uid(const ArchiveReader& reader) {
  auto uid = int64_t{ };
  auto uid_string = reader.read("uid");
  auto ss = std::istringstream(std::string(as_string_view(uid_string)));
  ss >> std::hex >> uid;
  return uid;
}

void for_each_archive_file(const ArchiveReader& reader,
    std::function<void(ArchiveFile)> file_callback) {

  auto header = reader.read("headers");
  auto header_store = HeaderStore();
  header_store.deserialize(as_string_view(header));
  for (const auto& entry : header_store.entries()) {
    const auto info = reader.get_file_info(to_local_filename(entry.first));
    if (info.has_value())
      file_callback(ArchiveFile{
        entry.first,
        info->compressed_size,
        info->uncompressed_size,
        info->modification_time
      });
  }
}

void for_each_archive_html(const ArchiveReader& reader,
    std::function<void(ArchiveHtml)> file_callback) {

  const auto url = std::string(as_string_view(reader.read("url")));
  const auto base_hostname = get_hostname(url);

  auto header = reader.read("headers");
  auto header_store = HeaderStore();
  header_store.deserialize(as_string_view(header));
  for (const auto& entry : header_store.entries()) {
    const auto hostname = get_hostname(entry.first);
    if (hostname != base_hostname)
      continue;
    const auto& header = entry.second.header;
    if (entry.second.status_code != StatusCode::success_ok)
      continue;
    if (auto it = header.find("Content-Length"); it != header.end())
      if (it->second == "0")
        continue;
    if (auto it = header.find("Content-Type"); it != header.end()) {
      const auto [mime_type, charset] = split_content_type(it->second);
      if (is_html_or_plaintext_mime_type(mime_type)) {
        auto data = reader.read(to_local_filename(entry.first));
        if (!data.empty())
          file_callback({
            entry.first,
            convert_charset(data, charset, "UTF-8")
          });
      }
    }
  }
}

void for_each_html_text(std::string_view html,
    std::function<void(std::string_view, HtmlSection)> text_callback) {

  const auto output = gumbo_parse_with_options(
    &kGumboDefaultOptions, html.data(), html.size());

  if (output->root->type != GUMBO_NODE_ELEMENT)
    return;

  const auto rec = [&](const GumboElement& element, const auto& rec,
      HtmlSection section, bool in_list) {

    switch (element.tag) {
      case GUMBO_TAG_TITLE:
        section = HtmlSection::title;
        break;

      case GUMBO_TAG_H1:
      case GUMBO_TAG_H2:
      case GUMBO_TAG_H3:
      case GUMBO_TAG_H4:
      case GUMBO_TAG_H5:
      case GUMBO_TAG_H6:
        if (section == HtmlSection::content)
          section = HtmlSection::heading;
        break;

      case GUMBO_TAG_NAV:
      case GUMBO_TAG_HEADER:
      case GUMBO_TAG_FOOTER:
      case GUMBO_TAG_ASIDE:
        section = HtmlSection::navigation;
        break;

      case GUMBO_TAG_SCRIPT:
      case GUMBO_TAG_STYLE:
      case GUMBO_TAG_NOSCRIPT:
      case GUMBO_TAG_TEXTAREA:
        return;

      case GUMBO_TAG_UL:
        in_list = true;
        break;

      case GUMBO_TAG_A:
        if (section == HtmlSection::content && in_list)
          section = HtmlSection::navigation;
        break;

      default:
        if (section == HtmlSection::content)
          if (const auto id = gumbo_get_attribute(&element.attributes, "id"))
            if (std::strstr(id->value, "header") ||
                std::strstr(id->value, "footer") ||
                std::strstr(id->value, "menu"))
              section = HtmlSection::navigation;
        break;
    }

    for (auto i = 0u; i < element.children.length; ++i) {
      const auto& child = *static_cast<const GumboNode*>(element.children.data[i]);
      if (child.type == GUMBO_NODE_TEXT) {
        auto text = std::string_view(child.v.text.original_text.data,
                                     child.v.text.original_text.length);
        text = trim(text);
        if (!text.empty())
          text_callback(text, section);
      }
      else if (child.type == GUMBO_NODE_ELEMENT) {
        rec(child.v.element, rec, section, in_list);
      }
    }
  };
  rec(output->root->v.element, rec, HtmlSection::content, false);

  gumbo_destroy_output(&kGumboDefaultOptions, output);
}

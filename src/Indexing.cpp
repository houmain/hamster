
#include "Indexing.h"
#include "gumbo.h"
#include "libs/webrecorder/src/Archive.h"
#include "libs/webrecorder/src/HeaderStore.h"

bool for_each_archive_html(const std::filesystem::path& filename,
    std::function<void(ArchiveHtml)> file_callback) {

  auto reader = ArchiveReader();
  if (!reader.open(filename))
    return false;

  auto uid = int64_t{ };
  auto uid_string = reader.read("uid");
  auto ss = std::istringstream(std::string(as_string_view(uid_string)));
  ss >> std::hex >> uid;
  if (!uid)
    return false;

  auto header = reader.read("headers");
  auto header_store = HeaderStore();
  header_store.deserialize(as_string_view(header));
  for (const auto& entry : header_store.entries()) {
    const auto& header = entry.second.header;
    if (entry.second.status_code != StatusCode::success_ok)
      continue;
    if (auto it = header.find("Content-Length"); it != header.end())
      if (it->second == "0")
        continue;
    if (auto it = header.find("Content-Type"); it != header.end()) {
      const auto [mime_type, charset] = split_content_type(it->second);
      if (iequals(mime_type, "text/html")) {
        auto data = reader.read(to_local_filename(entry.first));
        if (!data.empty())
          file_callback({
            uid,
            entry.first,
            as_string_view(data)
          });
      }
    }
  }
  return true;
}

void for_html_text(std::string_view html,
    std::function<void(std::string_view)> text_callback) {

  const auto output = gumbo_parse_with_options(
    &kGumboDefaultOptions, html.data(), html.size());

  if (output->root->type != GUMBO_NODE_ELEMENT)
    return;

  const auto rec = [&](const GumboElement& element, const auto& rec) {
    switch (element.tag) {
      case GUMBO_TAG_TITLE:
      case GUMBO_TAG_SCRIPT:
      case GUMBO_TAG_STYLE:
      case GUMBO_TAG_NOSCRIPT:
      case GUMBO_TAG_TEXTAREA:
        return;
      default:
        break;
    }
    for (auto i = 0u; i < element.children.length; ++i) {
      const auto& child = *static_cast<const GumboNode*>(element.children.data[i]);
      if (child.type == GUMBO_NODE_TEXT) {
        auto text = std::string_view(child.v.text.original_text.data,
                                     child.v.text.original_text.length);
        text = trim(text);
        if (!text.empty())
          text_callback(text);
      }
      else if (child.type == GUMBO_NODE_ELEMENT) {
        rec(child.v.element, rec);
      }
    }
  };
  rec(output->root->v.element, rec);

  gumbo_destroy_output(&kGumboDefaultOptions, output);
}

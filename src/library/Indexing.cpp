
#include "Indexing.h"
#include "gumbo.h"
#include "HeaderStore.h"
#include "platform.h"
#include "decode_content.h"
#include <cctype>
#include <cstring>
#include <set>
#include <sstream>
#include <utility>

namespace {
  bool is_html_or_plaintext_mime_type(std::string_view mime_type) {
    return iequals_any(mime_type, "text/html", "text/plain", "text", "html", "plain");
  }

  char ascii_lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  }

  bool ascii_iequal(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
      return false;
    for (auto i = size_t{}; i < a.size(); ++i)
      if (ascii_lower(a[i]) != ascii_lower(b[i]))
        return false;
    return true;
  }

  bool ascii_starts_with(std::string_view text, size_t offset, std::string_view prefix) {
    return offset <= text.size() && prefix.size() <= text.size() - offset &&
      ascii_iequal(text.substr(offset, prefix.size()), prefix);
  }

  void append_utf8(std::string& result, unsigned value) {
    if (!value || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
      value = 0xfffd;
    if (value <= 0x7f)
      result += static_cast<char>(value);
    else if (value <= 0x7ff) {
      result += static_cast<char>(0xc0 | value >> 6);
      result += static_cast<char>(0x80 | (value & 0x3f));
    }
    else if (value <= 0xffff) {
      result += static_cast<char>(0xe0 | value >> 12);
      result += static_cast<char>(0x80 | (value >> 6 & 0x3f));
      result += static_cast<char>(0x80 | (value & 0x3f));
    }
    else {
      result += static_cast<char>(0xf0 | value >> 18);
      result += static_cast<char>(0x80 | (value >> 12 & 0x3f));
      result += static_cast<char>(0x80 | (value >> 6 & 0x3f));
      result += static_cast<char>(0x80 | (value & 0x3f));
    }
  }

  std::string css_unescape(std::string_view value) {
    auto result = std::string{};
    for (auto i = size_t{}; i < value.size(); ++i) {
      if (value[i] != '\\' || i + 1 == value.size()) {
        result += value[i];
        continue;
      }
      ++i;
      if (value[i] == '\n' || value[i] == '\r' || value[i] == '\f')
        continue;
      if (std::isxdigit(static_cast<unsigned char>(value[i]))) {
        auto codepoint = 0u;
        auto digits = 0u;
        while (i < value.size() && digits < 6 &&
          std::isxdigit(static_cast<unsigned char>(value[i]))) {
          const auto c = ascii_lower(value[i]);
          codepoint = codepoint * 16 + static_cast<unsigned>(
            c >= '0' && c <= '9' ? c - '0' : c - 'a' + 10);
          ++i;
          ++digits;
        }
        if (i < value.size() && std::isspace(static_cast<unsigned char>(value[i]))) {
          if (value[i] == '\r' && i + 1 < value.size() && value[i + 1] == '\n')
            ++i;
        }
        else
          --i;
        append_utf8(result, codepoint);
      }
      else
        result += value[i];
    }
    return result;
  }

  void add_resolved(std::set<std::string>& result,
    std::string_view base, std::string_view reference) {
    if (auto url = resolve_http_url(base, reference))
      result.insert(std::move(*url));
  }

  const char* attribute(const GumboElement& element, const char* name) {
    if (const auto value = gumbo_get_attribute(&element.attributes, name))
      return value->value;
    return nullptr;
  }

  template <typename Callback>
  void for_each_element(const GumboNode& node, const Callback& callback) {
    if (node.type != GUMBO_NODE_ELEMENT)
      return;
    callback(node.v.element);
    for (auto i = 0u; i < node.v.element.children.length; ++i)
      for_each_element(*static_cast<const GumboNode*>(node.v.element.children.data[i]), callback);
  }

  bool token_contains(std::string_view tokens, std::string_view wanted) {
    for (auto begin = size_t{}; begin < tokens.size();) {
      while (begin < tokens.size() && std::isspace(static_cast<unsigned char>(tokens[begin])))
        ++begin;
      auto end = begin;
      while (end < tokens.size() && !std::isspace(static_cast<unsigned char>(tokens[end])))
        ++end;
      if (ascii_iequal(tokens.substr(begin, end - begin), wanted))
        return true;
      begin = end;
    }
    return false;
  }

  bool resource_link(const GumboElement& element) {
    const auto rel = attribute(element, "rel");
    if (!rel)
      return false;
    for (const auto wanted : { "stylesheet", "preload", "modulepreload", "prefetch",
           "icon", "manifest" })
      if (token_contains(rel, wanted))
        return true;
    return false;
  }

  void add_srcset(std::set<std::string>& result,
    std::string_view base, std::string_view srcset) {
    for (auto begin = size_t{}; begin < srcset.size();) {
      while (begin < srcset.size() &&
        (srcset[begin] == ',' || std::isspace(static_cast<unsigned char>(srcset[begin]))))
        ++begin;
      auto end = begin;
      while (end < srcset.size() && !std::isspace(static_cast<unsigned char>(srcset[end])))
        ++end;
      auto candidate = srcset.substr(begin, end - begin);
      while (!candidate.empty() && candidate.back() == ',')
        candidate.remove_suffix(1);
      add_resolved(result, base, candidate);
      begin = srcset.find(',', end);
      if (begin == std::string_view::npos)
        break;
      ++begin;
    }
  }

  size_t skip_css_space_and_comments(std::string_view css, size_t offset) {
    for (;;) {
      while (offset < css.size() && std::isspace(static_cast<unsigned char>(css[offset])))
        ++offset;
      if (offset + 1 >= css.size() || css[offset] != '/' || css[offset + 1] != '*')
        return offset;
      const auto end = css.find("*/", offset + 2);
      if (end == std::string_view::npos)
        return css.size();
      offset = end + 2;
    }
  }

  std::pair<std::string, size_t> read_css_string(
    std::string_view css, size_t offset, char terminator) {
    auto value = std::string{};
    while (offset < css.size()) {
      const auto c = css[offset++];
      if (c == terminator)
        break;
      if (c == '\\' && offset < css.size()) {
        value += c;
        value += css[offset++];
      }
      else
        value += c;
    }
    return { css_unescape(value), offset };
  }
} // namespace

int64_t get_archive_uid(const ArchiveReader& reader) {
  auto uid = int64_t{};
  auto uid_string = reader.read("uid");
  auto ss = std::istringstream(std::string(as_string_view(uid_string)));
  ss >> std::hex >> uid;
  return uid;
}

void for_each_archive_file(
  const ArchiveReader& reader, std::function<void(ArchiveFile)> file_callback) {

  auto header = reader.read("headers");
  auto header_store = HeaderStore();
  header_store.deserialize(as_string_view(header));
  for (const auto& entry : header_store.entries()) {
    const auto info = reader.get_file_info(to_local_filename(entry.first));
    if (info.has_value())
      file_callback(ArchiveFile{
        entry.first, info->compressed_size, info->uncompressed_size, info->modification_time });
  }
}

void for_each_archive_html(
  const ArchiveReader& reader, std::function<void(ArchiveHtml)> file_callback) {

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
        const auto filename = to_local_filename(entry.first);
        const auto info = reader.get_file_info(filename);
        if (!info || info->uncompressed_size > 32 * 1024 * 1024)
          continue;
        auto data = reader.read(filename);
        const auto encoding = header.find("Content-Encoding");
        auto text = decode_content(data, encoding == header.end() ? "" : encoding->second);
        if (!text) {
          log(Event::error, "Cannot decode archive text for indexing: ", entry.first);
          continue;
        }
        const auto decoded = convert_charset(*text, charset, "UTF-8");
        if (!decoded.empty())
          file_callback({ entry.first, decoded, !iequals_any(mime_type, "text/html", "html") });
      }
    }
  }
}

void for_each_html_text(
  std::string_view html, std::function<void(std::string_view, HtmlSection)> text_callback) {

  const auto output = gumbo_parse_with_options(&kGumboDefaultOptions, html.data(), html.size());

  if (output->root->type != GUMBO_NODE_ELEMENT) {
    gumbo_destroy_output(&kGumboDefaultOptions, output);
    return;
  }

  const auto rec = [&](const GumboElement& element, const auto& rec, HtmlSection section,
    bool in_list) {
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
            if (std::strstr(id->value, "header") || std::strstr(id->value, "footer") ||
              std::strstr(id->value, "menu"))
              section = HtmlSection::navigation;
        break;
      }

      for (auto i = 0u; i < element.children.length; ++i) {
        const auto& child = *static_cast<const GumboNode*>(element.children.data[i]);
        if (child.type == GUMBO_NODE_TEXT) {
          auto text =
            std::string_view(child.v.text.original_text.data, child.v.text.original_text.length);
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

void for_each_css_resources(std::string_view stylesheet_url, std::string_view css,
  std::function<void(std::string)> resource_callback) {
  auto resources = std::set<std::string>{};
  for (auto i = size_t{}; i < css.size();) {
    if (i + 1 < css.size() && css[i] == '/' && css[i + 1] == '*') {
      const auto end = css.find("*/", i + 2);
      i = end == std::string_view::npos ? css.size() : end + 2;
      continue;
    }
    if (css[i] == '\'' || css[i] == '"') {
      i = read_css_string(css, i + 1, css[i]).second;
      continue;
    }
    const auto identifier_before = i &&
      (std::isalnum(static_cast<unsigned char>(css[i - 1])) ||
        css[i - 1] == '_' || css[i - 1] == '-');
    if (!identifier_before && ascii_starts_with(css, i, "url")) {
      auto open = skip_css_space_and_comments(css, i + 3);
      if (open < css.size() && css[open] == '(') {
        auto value_begin = skip_css_space_and_comments(css, open + 1);
        auto value = std::string{};
        if (value_begin < css.size() && (css[value_begin] == '\'' || css[value_begin] == '"')) {
          const auto parsed = read_css_string(css, value_begin + 1, css[value_begin]);
          value = parsed.first;
          i = css.find(')', parsed.second);
        }
        else {
          const auto close = css.find(')', value_begin);
          const auto raw = trim(css.substr(value_begin,
            close == std::string_view::npos ? css.size() - value_begin : close - value_begin));
          value = css_unescape(raw);
          i = close;
        }
        add_resolved(resources, stylesheet_url, value);
        i = i == std::string_view::npos ? css.size() : i + 1;
        continue;
      }
    }
    if (css[i] == '@' && ascii_starts_with(css, i + 1, "import")) {
      auto value_begin = skip_css_space_and_comments(css, i + 7);
      if (value_begin < css.size() && (css[value_begin] == '\'' || css[value_begin] == '"')) {
        const auto parsed = read_css_string(css, value_begin + 1, css[value_begin]);
        add_resolved(resources, stylesheet_url, parsed.first);
        i = parsed.second;
        continue;
      }
    }
    ++i;
  }
  for (const auto& resource : resources)
    resource_callback(resource);
}

void for_each_html_resource(std::string_view document_url, std::string_view html,
  std::function<void(std::string)> resource_callback) {
  auto resources = std::set<std::string>{};
  const auto output = gumbo_parse_with_options(&kGumboDefaultOptions, html.data(), html.size());
  if (!output || !output->root) {
    if (output)
      gumbo_destroy_output(&kGumboDefaultOptions, output);
    return;
  }
  auto base = std::string(document_url);
  auto found_base = false;
  for_each_element(*output->root, [&](const GumboElement& element) {
    if (!found_base && element.tag == GUMBO_TAG_BASE)
      if (const auto href = attribute(element, "href"))
        if (auto resolved = resolve_http_url(document_url, href)) {
          base = std::move(*resolved);
          found_base = true;
        }
    });
  for_each_element(*output->root, [&](const GumboElement& element) {
    const auto add_attribute = [&](const char* name) {
      if (const auto value = attribute(element, name))
        add_resolved(resources, base, value);
      };
    switch (element.tag) {
    case GUMBO_TAG_SCRIPT:
    case GUMBO_TAG_IMG:
    case GUMBO_TAG_IMAGE:
    case GUMBO_TAG_IFRAME:
    case GUMBO_TAG_FRAME:
    case GUMBO_TAG_EMBED:
    case GUMBO_TAG_AUDIO:
    case GUMBO_TAG_SOURCE:
    case GUMBO_TAG_TRACK:
    case GUMBO_TAG_INPUT:
      add_attribute("src");
      break;
    case GUMBO_TAG_OBJECT:
      add_attribute("data");
      break;
    case GUMBO_TAG_VIDEO:
      add_attribute("src");
      add_attribute("poster");
      break;
    case GUMBO_TAG_LINK:
      if (resource_link(element))
        add_attribute("href");
      break;
    default:
      break;
    }
    if (element.tag == GUMBO_TAG_IMG || element.tag == GUMBO_TAG_SOURCE)
      if (const auto value = attribute(element, "srcset"))
        add_srcset(resources, base, value);
    if (const auto style = attribute(element, "style"))
      for_each_css_resources(base, style,
        [&](std::string resource) { resources.insert(std::move(resource)); });
    if (element.tag == GUMBO_TAG_STYLE)
      for (auto i = 0u; i < element.children.length; ++i) {
        const auto& child = *static_cast<const GumboNode*>(element.children.data[i]);
        if (child.type == GUMBO_NODE_TEXT || child.type == GUMBO_NODE_CDATA)
          for_each_css_resources(base, child.v.text.text,
            [&](std::string resource) { resources.insert(std::move(resource)); });
      }
    });
  gumbo_destroy_output(&kGumboDefaultOptions, output);
  for (const auto& resource : resources)
    resource_callback(resource);
}

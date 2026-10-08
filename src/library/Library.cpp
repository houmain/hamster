#include "Library.h"
#include "Archive.h"
#include "platform.h"
#include <cwctype>
#include <fstream>
#include <regex>
#include <sstream>
#include <utility>

void Library::for_archives(const std::filesystem::path& p,
  const std::function<void(const std::filesystem::path&, int64_t)>& action) const {
  if (std::filesystem::is_directory(p)) {
    for (const auto& entry : std::filesystem::directory_iterator(p)) {
      if (entry.is_symlink() || path_to_utf8(entry.path().filename()).front() == '.')
        continue;
      // Revalidate descendants to reject junctions escaping the library.
      const auto child = path(entry.path().lexically_relative(m_root).generic_u8string());
      for_archives(child, action);
    }
  }
  else if (std::filesystem::is_regular_file(p)) {
    idle(p);
    auto reader = ArchiveReader{};
    if (reader.open_root(p)) {
      const auto uid = get_archive_uid(reader);
      reader.close();
      action(p, uid);
    }
  }
}

void Library::idle(const std::filesystem::path& p) const {
  for (const auto& [_, paths] : m_active)
    for (const auto& a : { paths.first, paths.second }) {
      if (a.empty())
        continue;
      auto compared_a = a, compared_p = p;
#ifdef _WIN32
      auto lower = [](const std::filesystem::path& value) {
        auto text = value.native();
        for (auto& c : text)
          c = static_cast<wchar_t>(std::towlower(c));
        return std::filesystem::path(text);
        };
      compared_a = lower(a);
      compared_p = lower(p);
#endif
      const auto relative = compared_a.lexically_relative(compared_p);
      if (compared_a == compared_p || (!relative.empty() && *relative.begin() != ".."))
        throw std::runtime_error("Archive is in use; close its recording tabs first");
    }
}

void Library::move(const std::filesystem::path& from, const std::filesystem::path& to) {
  idle(from);
  idle(to);
  if (from == to || !std::filesystem::exists(from))
    return;
  if (std::filesystem::exists(to))
    throw std::runtime_error("Destination already exists");
  std::filesystem::create_directories(to.parent_path());
  std::filesystem::rename(from, to);
}

Library::Library(std::filesystem::path directory)
  : m_root(std::filesystem::weakly_canonical(directory)) {}

std::filesystem::path Library::committed(const std::string& id) {
  const auto it = m_active.find(id);
  if (it == m_active.end())
    return {};
  auto output = std::move(it->second.second);
  m_active.erase(it);
  return output;
}

const std::filesystem::path& Library::root() const {
  return m_root;
}

std::filesystem::path Library::path(const std::string& name, bool internal) const {
  if (name.empty())
    return {};
  if (name.front() == '/' || name.find('\\') != std::string::npos ||
    name.find(':') != std::string::npos || name.find('\0') != std::string::npos)
    throw std::runtime_error("Expected a relative archive path");
  auto relative = std::filesystem::path{};
  auto parts = std::istringstream(name);
  auto part = std::string{};
  while (std::getline(parts, part, '/')) {
    if (part.empty() || part == "." || part == ".." || (!internal && part.front() == '.'))
      throw std::runtime_error("Invalid archive path component");
    const auto safe = get_legal_filename(part);
    if (safe.empty() || safe == "." || safe == ".." || safe.back() == '.' || safe.back() == ' ')
      throw std::runtime_error("Invalid archive filename");
    static const auto device =
      std::regex("^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])([.].*)?$", std::regex::icase);
    if (std::regex_match(safe, device))
      throw std::runtime_error("Reserved archive filename");
    relative /= utf8_to_path(safe);
  }
  const auto base = std::filesystem::weakly_canonical(m_root);
  auto result = base;
  // Reject links/junctions leading outside the root, including existing ancestors.
  for (const auto& part : relative) {
    result /= part;
    const auto resolved = std::filesystem::weakly_canonical(result);
    const auto rel = resolved.lexically_relative(base);
    if (rel.empty() || rel == "." || *rel.begin() == "..")
      throw std::runtime_error("Archive path escapes library folder");
  }
  return std::filesystem::weakly_canonical(result);
}

void Library::validate_undo_id(const std::string& id) {
  if (id.empty() ||
    id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") !=
    std::string::npos)
    throw std::runtime_error("Invalid undo ID");
}

std::vector<int64_t> Library::remove(const std::string& name, const std::string& undo_id) {
  validate_undo_id(undo_id);
  const auto manifest = path(".trash/" + undo_id + "/path.txt", true);
  const auto data = path(".trash/" + undo_id + "/archive", true);
  const auto source = path(name);
  if (source.empty())
    throw std::runtime_error("Empty archive path");
  if (!std::filesystem::exists(source))
    return {};
  idle(source);
  if (std::filesystem::exists(manifest) || std::filesystem::exists(data))
    throw std::runtime_error("Undo ID already used");
  std::filesystem::create_directories(manifest.parent_path());
  auto out = std::ofstream(manifest);
  out << name;
  out.close();
  if (!out)
    throw std::runtime_error("Cannot save undo information");
  auto removed = std::vector<int64_t>{};
  for_archives(source, [&](const auto&, int64_t uid) { removed.push_back(uid); });
  move(source, data);
  return removed;
}

std::optional<std::filesystem::path> Library::restore(const std::string& undo_id) {
  validate_undo_id(undo_id);
  const auto manifest = path(".trash/" + undo_id + "/path.txt", true);
  const auto data = path(".trash/" + undo_id + "/archive", true);
  if (!std::filesystem::exists(data))
    return {};
  auto in = std::ifstream(manifest);
  auto name = std::string((std::istreambuf_iterator<char>(in)), {});
  in.close();
  if (!in)
    throw std::runtime_error("Cannot read undo information");
  const auto target = path(name);
  if (target.empty())
    throw std::runtime_error("Invalid undo information");
  move(data, target);
  std::filesystem::remove(manifest);
  std::filesystem::remove(manifest.parent_path());
  return target;
}

std::optional<uintmax_t> Library::size(const std::filesystem::path& selected) const {
  if (!std::filesystem::is_regular_file(selected))
    return {};
  return std::filesystem::file_size(selected);
}

std::vector<ArchiveFile> Library::files(const std::filesystem::path& selected) const {
  // Recordings are written to a temporary archive and only replace this path
  // when the session is committed. Reading the committed archive is safe while recording.
  auto result = std::vector<ArchiveFile>{};
  auto reader = ArchiveReader{};
  if (reader.open(selected))
    for_each_archive_file(reader,
      [&](ArchiveFile file) { result.push_back(std::move(file)); });
  return result;
}

std::vector<LibraryArchive> Library::list() const {
  auto result = std::vector<LibraryArchive>{};
  for (auto it = std::filesystem::recursive_directory_iterator(m_root);
    it != std::filesystem::recursive_directory_iterator(); ++it) {
    if (it->path().filename().native().front() == '.' || it->is_symlink()) {
      it.disable_recursion_pending();
      continue;
    }
    const auto name = it->path().lexically_relative(m_root).generic_u8string();
    // On Windows directory iterators use backslashes; generic form is the protocol form.
    const auto safe = path(name);
    if (!std::filesystem::is_regular_file(safe))
      continue;
    auto reader = ArchiveReader{};
    if (reader.open_root(safe)) {
      const auto url = reader.read("url");
      if (!url.empty())
        result.push_back({ name, std::string(as_string_view(url)) });
    }
  }
  return result;
}

void Library::opened(const std::string& id, const std::filesystem::path& input,
  const std::filesystem::path& output) {
  m_active[id] = { input, output };
}

std::vector<std::filesystem::path> Library::committed_all() {
  auto outputs = std::vector<std::filesystem::path>{};
  while (!m_active.empty()) {
    auto output = committed(m_active.begin()->first);
    if (!output.empty())
      outputs.push_back(std::move(output));
  }
  return outputs;
}

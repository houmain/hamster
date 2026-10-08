
#include "HostList.h"
#include "platform.h"

void HostList::add_hosts_from_file(const std::filesystem::path& filename) {
  m_hosts_files.emplace_back(read_utf8_textfile(filename));
  const auto view = std::string_view(m_hosts_files.back());
  for (auto pos = std::string::size_type{ }; ;) {
    const auto begin = pos;
    pos = view.find('\n', pos);
    if (pos == std::string_view::npos)
      break;
    auto line = view.substr(begin, pos - begin);
    ++pos;

    if (auto hash = line.find('#'); hash != std::string_view::npos)
      line = line.substr(0, hash);
    line = trim(line);
    if (starts_with(line, "0.0.0.0"))
      line = line.substr(7);
    line = trim(line);
    if (!line.empty() && line.find(' ') == std::string_view::npos)
      m_hash_set.emplace(line);
  }
}

bool HostList::contains(std::string_view url) const {
  auto domain = get_hostname_port(url);
  for (;;) {
    if (m_hash_set.find(domain) != m_hash_set.end())
      return true;
    domain = get_without_first_domain(domain);
    if (domain.empty())
      return false;
  }
}

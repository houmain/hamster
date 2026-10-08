#include "ResourceGraph.h"
#include "common.h"
#include <stdexcept>

std::string ResourceGraph::request_url(std::string url) {
  const auto fragment = url.find('#');
  if (fragment != std::string::npos)
    url.erase(fragment);
  if (url.empty() || !iequals_any(get_scheme(url), "http", "https"))
    throw std::invalid_argument("Expected an absolute HTTP resource URL");
  return url;
}

bool ResourceGraph::claim(const ResourceOwner& owner, std::string url) {
  if (owner.bookmark.empty())
    throw std::invalid_argument("Invalid resource owner");
  url = request_url(std::move(url));
  auto lock = std::lock_guard(m_mutex);
  const auto added = m_owners_by_url[url].insert(owner).second;
  if (added)
    m_urls_by_owner[owner].insert(std::move(url));
  return added;
}

std::vector<ResourceOwner> ResourceGraph::owners(std::string url) const {
  url = request_url(std::move(url));
  auto lock = std::lock_guard(m_mutex);
  const auto found = m_owners_by_url.find(url);
  if (found == m_owners_by_url.end())
    return {};
  return { found->second.begin(), found->second.end() };
}

bool ResourceGraph::contains_https_authority(const std::string& authority) const {
  auto normalized = authority;
  if (normalized.size() > 4 && normalized.compare(normalized.size() - 4, 4, ":443") == 0)
    normalized.resize(normalized.size() - 4);
  auto lock = std::lock_guard(m_mutex);
  for (const auto& [url, owners] : m_owners_by_url) {
    if (owners.empty() || !iequals(get_scheme(url), "https"))
      continue;
    auto resource_authority = std::string(get_hostname_port(url));
    if (resource_authority.size() > 4 &&
      resource_authority.compare(resource_authority.size() - 4, 4, ":443") == 0)
      resource_authority.resize(resource_authority.size() - 4);
    if (iequals(resource_authority, normalized))
      return true;
  }
  return false;
}

void ResourceGraph::release(const ResourceOwner& owner) {
  auto lock = std::lock_guard(m_mutex);
  const auto reverse = m_urls_by_owner.find(owner);
  if (reverse == m_urls_by_owner.end())
    return;
  for (const auto& url : reverse->second) {
    const auto found = m_owners_by_url.find(url);
    if (found == m_owners_by_url.end())
      continue;
    found->second.erase(owner);
    if (found->second.empty())
      m_owners_by_url.erase(found);
  }
  m_urls_by_owner.erase(reverse);
}

void ResourceGraph::clear() {
  auto lock = std::lock_guard(m_mutex);
  m_owners_by_url.clear();
  m_urls_by_owner.clear();
}

size_t ResourceGraph::resource_count() const {
  auto lock = std::lock_guard(m_mutex);
  return m_owners_by_url.size();
}

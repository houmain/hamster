#pragma once

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

struct ResourceOwner {
  std::string bookmark;

  bool operator==(const ResourceOwner& rhs) const {
    return bookmark == rhs.bookmark;
  }
  bool operator<(const ResourceOwner& rhs) const {
    return bookmark < rhs.bookmark;
  }
};

// Tracks the active recordings which have claimed each resource URL. URL fragments
// are deliberately ignored because they are not part of an HTTP request target.
class ResourceGraph final {
public:
  bool claim(const ResourceOwner& owner, std::string url);
  std::vector<ResourceOwner> owners(std::string url) const;
  bool contains_https_authority(const std::string& authority) const;
  void release(const ResourceOwner& owner);
  void clear();

  size_t resource_count() const;

private:
  static std::string request_url(std::string url);

  mutable std::mutex m_mutex;
  std::map<std::string, std::set<ResourceOwner>> m_owners_by_url;
  std::map<ResourceOwner, std::set<std::string>> m_urls_by_owner;
};

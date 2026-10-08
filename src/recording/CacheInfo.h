#pragma once

#include "proxy/ProxyRequest.h"
#include <optional>

struct CacheInfo {
  bool expired;
  std::time_t last_modified_time;
  std::string etag;
};

std::optional<CacheInfo> get_cache_info(StatusCode status_code,
    const Header& reply_header, const Header& request_header);

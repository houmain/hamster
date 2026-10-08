#pragma once

#include "common.h"
#include <optional>
#include <string>

// Decodes an HTTP Content-Encoding chain in reverse application order. The
// output limit also bounds decompression bombs used by indexing/discovery.
std::optional<std::string> decode_content(
  ByteView data, std::string_view encoding, size_t limit = 32 * 1024 * 1024);

#pragma once

#include <stdexcept>
#include <string>
#include <functional>
#include <optional>
#include "libs/rapidjson/document.h"
#include "libs/rapidjson/stringbuffer.h"
#include "libs/rapidjson/writer.h"

namespace json {

struct Exception : std::runtime_error {
  using runtime_error::runtime_error;
};

using Document = rapidjson::Document;
using Value = rapidjson::Value;
using Writer = rapidjson::Writer<rapidjson::StringBuffer>;

Document parse(std::string_view message);
bool get_bool(const Value& message, const char* name);
std::optional<bool> try_get_bool(const Value& message, const char* name);
int get_int(const Value& message, const char* name);
std::optional<int> try_get_int(const Value& message, const char* name);
std::string_view get_string(const Value& value, const char* name);
std::optional<std::string_view> try_get_string(const Value& value, const char* name);
std::vector<int> get_int_list(const Value& message, const char* name);
std::vector<std::string_view> get_string_list(const Value& message, const char* name);
std::string build_string(const std::function<void(Writer&)>& write);

} // namespace

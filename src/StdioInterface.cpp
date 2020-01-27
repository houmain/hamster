
#include "StdioInterface.h"

StdioInterface::StdioInterface(Logic* logic)
  : m_logic(*logic) {
}

void StdioInterface::handle_request(Response& response, const Request& request) {
  response.StartObject();
  response.Key("requestId");
  response.Int(json::get_int(request, "requestId"));
  m_logic.handle_request(response, request);
  response.EndObject();
}

void StdioInterface::handle_error(Response& response,
    const json::Document& request, const std::exception& ex) {
  response.StartObject();
  response.Key("requestId");
  response.Int(json::get_int(request, "requestId"));
  response.Key("error");
  response.String(ex.what());
  response.EndObject();
}

bool StdioInterface::read(std::vector<char>& buffer) {
  auto length = uint32_t{ };
  if (std::fread(&length, 1, 4, stdin) == 4) {
    buffer.resize(length);
    if (std::fread(buffer.data(), 1, length, stdin) == length)
      return true;
  }
  return false;
}

void StdioInterface::write(std::string_view message) {
  auto length = static_cast<uint32_t>(message.size());
  if (std::fwrite(&length, 1, 4, stdout) == 4) {
    std::fwrite(message.data(), 1, length, stdout);
    std::fflush(stdout);
  }
}

void StdioInterface::run() {
  auto buffer = std::vector<char>();
  while (read(buffer)) {
    auto request = json::parse(std::string_view(buffer.data(), buffer.size()));
    try {
      write(json::build_string([&](Response& response) {
        handle_request(response, request);
      }));
    }
    catch (const std::exception& ex) {
      write(json::build_string([&](Response& response) {
        handle_error(response, request, ex);
      }));
    }
  }
}

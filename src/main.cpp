
#include "platform.h"
#include "Settings.h"
#include "Logic.h"
#include "common.h"

namespace {
  void handle_request(Logic& logic, Response& response, const Request& request) {
    response.StartObject();
    if (auto request_id = json::try_get_int(request, "requestId")) {
      response.Key("requestId");
      response.Int(request_id.value());
    }
    logic.handle_request(response, request);
    response.EndObject();
  }

  void handle_error(Response& response,
      const json::Document& request, const std::exception& ex) {
    response.StartObject();
    if (auto request_id = json::try_get_int(request, "requestId")) {
      response.Key("requestId");
      response.Int(request_id.value());
    }
    response.Key("error");
    response.String(ex.what());
    response.EndObject();
  }

  std::string_view read_plain(std::vector<char>& buffer) {
    buffer.resize(1024);
    for (;;) {
      const auto line = std::fgets(buffer.data(),
        static_cast<int>(buffer.size()), stdin);
      if (!line)
        return { };
      if (!trim(line).empty())
        return std::string_view{ line };
    }
  }

  void write_plain(const std::string& message) {
    std::fprintf(stdout, "%s\n", message.c_str());
    std::fflush(stdout);
  }

  std::string_view read_binary(std::vector<char>& buffer) {
    auto length = uint32_t{ };
    if (std::fread(&length, 1, 4, stdin) == 4) {
      buffer.resize(length);
      if (std::fread(buffer.data(), 1, length, stdin) == length)
        return std::string_view(buffer.data(), length);
    }
    return { };
  }

  void write_binary(const std::string& message) {
    auto length = static_cast<uint32_t>(message.size());
    if (std::fwrite(&length, 1, 4, stdout) == 4) {
      std::fwrite(message.data(), 1, length, stdout);
      std::fflush(stdout);
    }
  }

  std::string_view read(bool plain, std::vector<char>& buffer) {
    return (plain ? read_plain(buffer) : read_binary(buffer));
  }

  void write(bool plain, const std::string& message) {
    return (plain ? write_plain(message) : write_binary(message));
  }
} // namespace

int run(int argc, const char* argv[]) noexcept try {
  auto settings = Settings{ };

  if (!interpret_commandline(settings, argc, argv)) {
    print_help_message(argv[0]);
    return 1;
  }

  auto logic = Logic(settings);

  auto buffer = std::vector<char>();
  for (;;) {
    auto line = read(settings.plain_stdio_interface, buffer);
    if (line.empty())
      break;
    auto request = json::parse(line);
    try {
      write(settings.plain_stdio_interface,
        json::build_string([&](Response& response) {
          handle_request(logic, response, request);
        }));
    }
    catch (const std::exception& ex) {
      write(settings.plain_stdio_interface,
        json::build_string([&](Response& response) {
          handle_error(response, request, ex);
        }));
    }
  }
  return 0;
}
catch (const std::exception& ex) {
  std::fprintf(stderr, "unhanded exception: %s\n", ex.what());
  return 1;
}

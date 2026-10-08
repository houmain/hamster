#include "native_host.h"
#include "proxy/run_proxy.h"
#include "Settings.h"
#include "platform.h"

#include <cstdint>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {

bool read_message(std::vector<char>& buffer) {
  auto length = uint32_t{};
  if (std::fread(&length, sizeof(length), 1, stdin) != 1)
    return false;
  if (length > 1024 * 1024)
    throw std::runtime_error("Native message is too large");
  buffer.resize(length);
  return std::fread(buffer.data(), 1, buffer.size(), stdin) == buffer.size();
}

// Native messaging always JSON-encodes a JavaScript string. Decode only that
// transport envelope; the payload itself is an ordinary command line.
std::string decode_string(std::string_view message) {
  if (message.size() < 2 || message.front() != '"' || message.back() != '"')
    throw std::runtime_error("Native startup command must be a string");
  auto result = std::string{};
  for (size_t i = 1; i + 1 < message.size(); ++i) {
    auto character = message[i];
    if (character != '\\') {
      result += character;
      continue;
    }
    if (++i + 1 >= message.size())
      throw std::runtime_error("Invalid native startup command");
    character = message[i];
    switch (character) {
      case '"': case '\\': case '/': result += character; break;
      case 'b': result += '\b'; break;
      case 'f': result += '\f'; break;
      case 'n': result += '\n'; break;
      case 'r': result += '\r'; break;
      case 't': result += '\t'; break;
      default: throw std::runtime_error("Unsupported escape in native startup command");
    }
  }
  return result;
}

std::vector<std::string> split_commandline(std::string_view commandline) {
  auto result = std::vector<std::string>{};
  for (size_t i = 0; i < commandline.size();) {
    while (i < commandline.size() && (commandline[i] == ' ' || commandline[i] == '\t'))
      ++i;
    if (i == commandline.size())
      break;
    auto argument = std::string{};
    auto quoted = false;
    auto backslashes = size_t{};
    while (i < commandline.size()) {
      const auto character = commandline[i++];
      if (character == '\\') {
        ++backslashes;
        continue;
      }
      if (character == '"') {
        argument.append(backslashes / 2, '\\');
        if (backslashes % 2)
          argument += '"';
        else
          quoted = !quoted;
        backslashes = 0;
        continue;
      }
      argument.append(backslashes, '\\');
      backslashes = 0;
      if (!quoted && (character == ' ' || character == '\t'))
        break;
      argument += character;
    }
    argument.append(backslashes, '\\');
    if (quoted)
      throw std::runtime_error("Unterminated quote in native startup command");
    result.push_back(std::move(argument));
  }
  return result;
}

} // namespace

bool is_native_invocation(int argc, const char* const argv[]) {
  // Firefox passes the native manifest path and extension ID as positional arguments.
  return argc > 1 && argv[1][0] != '-';
}

int run_native_host(int, const char* const argv[]) {
  native_stdio_active = true;
#ifdef _WIN32
  (void)_setmode(_fileno(stdin), _O_BINARY);
  (void)_setmode(_fileno(stdout), _O_BINARY);
#endif
  auto buffer = std::vector<char>();
  if (!read_message(buffer))
    return 0;
  const auto commandline = decode_string(std::string_view(buffer.data(), buffer.size()));
  auto arguments = split_commandline(commandline);
  arguments.insert(arguments.begin(), argv[0]);
  auto argument_pointers = std::vector<const char*>{};
  for (const auto& argument : arguments)
    argument_pointers.push_back(argument.c_str());

  auto settings = Settings{};
  if (!interpret_commandline(
      settings, static_cast<int>(argument_pointers.size()), argument_pointers.data()))
    throw std::runtime_error("Invalid native startup command line");

  // The native messaging connection is this browser's ownership lease. A
  // second browser keeps a standby host which takes over after the current
  // owner closes, while the instance lock guarantees one listener.
  auto connected = std::make_shared<std::atomic_bool>(true);
  std::thread([connected] {
    auto input = std::vector<char>();
    while (read_message(input)) {
      // The first message is the only command. Further input only keeps the pipe alive.
    }
    connected->store(false);
    request_proxy_stop();
  }).detach();

  auto instance = ProxyInstanceLock(settings.port);
  while (connected->load()) {
    if (instance.try_lock()) {
      if (!connected->load())
        return 0;
      return run_proxy(settings);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return 0;
}

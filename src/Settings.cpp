#include "Settings.h"
#include "common.h"
#include <regex>

namespace {
  constexpr auto hamster_version =
#include "_version.h"
  ;
} // namespace

bool interpret_commandline(Settings& settings, int argc, const char* argv[]) {
  const auto directory = std::filesystem::absolute(utf8_to_path(argv[0])).parent_path();
  settings.ca_directory = directory / "ca";
  settings.catalog_database = directory / "catalog.sqlite";
  if (directory.filename() == ".hamster") {
    settings.library_root = directory.parent_path();
  }
  else {
    settings.library_root = directory / "library";
  }
  for (auto i = 1; i < argc; ++i) {
    const auto option = std::string(argv[i]);
    if (++i == argc)
      return false;
    const auto value = std::string(unquote(argv[i]));
    if (value.empty())
      return false;
    if (option == "--bind")
      settings.listen_address = value;
    else if (option == "--ca-dir")
      settings.ca_directory = std::filesystem::absolute(utf8_to_path(value));
    else if (option == "--catalog")
      settings.catalog_database = std::filesystem::absolute(utf8_to_path(value));
    else if (option == "--library-root")
      settings.library_root = std::filesystem::absolute(utf8_to_path(value));
    else if (option == "--upstream-ca")
      settings.upstream_ca = value;
    else if (option == "--proxy")
      settings.proxy_server = value;
    else if (option == "--impersonate")
      settings.impersonate = value;
    else if (option == "--port" || option == "--request-timeout") {
      if (!std::regex_match(value, std::regex("[0-9]{1,5}")))
        return false;
      const auto number = std::stoi(value);
      if (option == "--port") {
        if (number > 65535)
          return false;
        settings.port = number;
      } else {
        if (number == 0)
          return false;
        settings.request_timeout = std::chrono::seconds(number);
      }
    }
    else
      return false;
  }
  return true;
}

void print_help_message(const char* argv0) {
  auto program = std::string(argv0);
  if (auto i = program.rfind('/'); i != std::string::npos)
    program = program.substr(i + 1);
  if (auto i = program.rfind('.'); i != std::string::npos)
    program = program.substr(0, i);

  printf(
    "hamster %s (c) 2020-2026 by Albert Kalchmair\n"
    "\n"
    "Usage: %s [--options]\n"
    "  --bind <address>       Listener address (default 0.0.0.0).\n"
    "  --port <port>          Listener port (default 8080).\n"
    "  --ca-dir <directory>   Persistent certificate-authority directory.\n"
    "  --catalog <file>       Catalog database file (default catalog.sqlite).\n"
    "  --library-root <dir>   Library directory.\n"
    "  --upstream-ca <file>   Override upstream TLS trust bundle.\n"
    "  --proxy <host:port>    Upstream proxy.\n"
    "  --impersonate <name>   Upstream browser profile (default firefox147).\n"
    "  --request-timeout <s>  Default upstream timeout (default 5 seconds).\n"
    "  --help                 Show this help.\n"
    "\n"
    "All Rights Reserved.\n"
    "This program comes with absolutely no warranty.\n"
    "See the GNU General Public License, version 3 for details.\n"
    "\n", hamster_version, program.c_str());
}

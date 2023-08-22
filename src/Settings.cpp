
#include "Settings.h"
#include "common.h"

namespace {
  const auto g_version =
#if __has_include("_version.h")
# include "_version.h"
#endif
    "";
} // namespace

bool interpret_commandline(Settings& settings, int argc, const char* argv[]) {
  settings.version = g_version;

  for (auto i = 1; i < argc; i++) {
    const auto argument = std::string_view(argv[i]);
    if (argument == "-p") {
      settings.plain_stdio_interface = true;
    }
    else if (argument == "-h" || argument == "--help") {
      return false;
    }
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
    "hamster %s (c) 2020-2023 by Albert Kalchmair\n"
    "\n"
    "Usage: %s [-options]\n"
    "  -p          run plain stdio JSON command interface.\n"
    "  -h, --help  print this help.\n"
    "\n"
    "All Rights Reserved.\n"
    "This program comes with absolutely no warranty.\n"
    "See the GNU General Public License, version 3 for details.\n"
    "\n", g_version, program.c_str());
}

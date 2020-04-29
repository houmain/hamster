#pragma once

#include <filesystem>

struct Settings {
  bool plain_stdio_interface{ };
};

bool interpret_commandline(Settings& settings, int argc, const char* argv[]);
void print_help_message(const char* argv0);

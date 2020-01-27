#pragma once

#include <filesystem>
#include <vector>

struct Settings {
  bool run_stdio_interface{ };
  std::vector<std::string> json_input;
  std::filesystem::path webrecorder_path;
  std::filesystem::path default_library_root;
};

bool interpret_commandline(Settings& settings, int argc, const char* argv[]);
void print_help_message(const char* argv0);

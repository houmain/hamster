#pragma once

#include <chrono>
#include <filesystem>
#include <string>

struct Settings {
  std::string listen_address{ "0.0.0.0" };
  int port{8080};
  std::filesystem::path library_root;
  std::filesystem::path ca_directory;
  std::filesystem::path catalog_database;
  std::string upstream_ca;
  std::string proxy_server;
  std::string impersonate{ "firefox147" };
  std::chrono::seconds request_timeout{ 5 };
};

bool interpret_commandline(Settings& settings, int argc, const char* argv[]);
void print_help_message(const char* argv0);

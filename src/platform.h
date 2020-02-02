#pragma once

#include <filesystem>

const std::filesystem::path& webrecorder_path();
const std::filesystem::path& default_library_root();

extern int run(int argc, const char* argv[]) noexcept;

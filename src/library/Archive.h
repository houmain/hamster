#pragma once

#include "common.h"
#include <functional>
#include <map>
#include <deque>
#include <filesystem>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <optional>

class ILossyCompressor;

class ArchiveReader final {
public:
  struct FileInfo {
    uint64_t compressed_size;
    uint64_t uncompressed_size;
    time_t modification_time;

    uint64_t directory_entry;
    uint64_t file_index;
  };

  enum FileVersion {
    top,
    overlay,
    base,
  };

  ArchiveReader() = default;
  ArchiveReader(const ArchiveReader&) = delete;
  ArchiveReader& operator=(const ArchiveReader&) = delete;
  ~ArchiveReader();

  void set_overlay_path(std::string path);
  bool open(const std::filesystem::path& filename);
  bool open_root(const std::filesystem::path& filename);
  void close();

  std::optional<FileInfo> get_file_info(
    const std::string& filename, FileVersion version = top) const;
  ByteVector read(const std::string& filename,
    FileVersion version = top) const;

  void for_each_file(const std::function<void(std::string)>& callback) const;

private:
  bool read_contents(bool only_root);
  void* acquire_context() const;
  void return_context(void* context) const;
  std::optional<FileInfo> do_get_file_info(const std::string& filename) const;
  ByteVector do_read(const std::string& filename) const;

  std::filesystem::path m_filename;
  std::string m_overlay_path;
  mutable std::mutex m_mutex;
  mutable std::vector<void*> m_unzip_contexts;
  std::map<std::string, FileInfo> m_contents;
};

//-------------------------------------------------------------------------

class ArchiveWriter final {
public:
  ArchiveWriter();
  ArchiveWriter(const ArchiveWriter&) = delete;
  ArchiveWriter& operator=(const ArchiveWriter&) = delete;
  ~ArchiveWriter();

  void set_lossy_compressor(std::unique_ptr<ILossyCompressor> lossy_compressor);
  bool open(std::filesystem::path filename);
  bool is_open() const { return !m_filename.empty(); }
  void move_on_close(std::filesystem::path filename, bool overwrite);
  bool close();
  bool write(const std::string& filename, ByteView data,
    time_t modification_time = 0, bool allow_lossy_compression = false);
  void async_write(const std::string& filename, ByteView data,
    time_t modification_time, bool allow_lossy_compression,
    std::function<void(bool)>&& on_complete);
  bool contains(const std::string& filename) const;
  std::optional<time_t> get_modification_time(const std::string& filename) const;
  void async_read(const std::string& filename,
    std::function<void(ByteVector, time_t)>&& on_complete);

private:
  bool update_contents(const std::string& filename, time_t modification_time);
  bool reopen(bool for_reading);
  void do_close();
  bool do_write(const std::string& filename, ByteView data,
    time_t modification_time, bool allow_lossy_compression);
  std::pair<ByteVector, time_t> do_read(const std::string& filename);

  void insert_task(std::function<void()>&& task);
  void start_thread();
  void finish_thread();
  void thread_func();

  std::filesystem::path m_filename;
  std::filesystem::path m_move_on_close;
  bool m_overwrite{ };
  std::map<std::string, time_t> m_contents;

  std::mutex m_zip_mutex;
  std::unique_ptr<ILossyCompressor> m_lossy_compressor;
  void* m_zip{ };
  bool m_reading{ };

  std::mutex m_tasks_mutex;
  std::condition_variable m_tasks_signal;
  std::deque<std::function<void()>> m_tasks;
  bool m_finish_thread{ };
  std::thread m_thread;
};

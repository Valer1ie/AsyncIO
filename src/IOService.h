#pragma once
#include "misc/traits.h"
#include "misc/utils.h"
#include <atomic>
#include <cassert>
#include <filesystem>
#include <functional>
#include <span>
#include <thread>
#include <variant>
#include <vector>

namespace John {
struct file_handle {
  void *file = nullptr;
  size_t length = 0;
};
struct FileDesc {
  file_handle handle;
  uint32_t offset;
  uint32_t size;
};

struct RawDataDesc {
  std::span<uint8_t> data;
};
using CmdTarget = std::variant<FileDesc, RawDataDesc>;
struct IOCmd {
  CmdTarget src;
  CmdTarget dst;
  uint32_t flags;
};
using IOCallBack = std::function<void(void)>;
struct Event {
  std::atomic_int64_t timeline;
  void Wait(uint64_t timeline) {
    while (this->timeline.load(std::memory_order_relaxed) < timeline) {
      std::this_thread::yield();
    }
  }
  void Signal(uint64_t timeline) {
    if (timeline > this->timeline.load(std::memory_order_relaxed))
      this->timeline.store(timeline, std::memory_order_relaxed);
  }
  bool IsSignaled(uint64_t timeline) {
    return this->timeline.load(std::memory_order_relaxed) >= timeline;
  }
};

struct IOService {
  static void Init();
  static void Dispose();

  static uint64_t Execute(class IOCommandList &cmd_list);
  static void Sync(uint64_t time_stamp);
  struct Impl;
};

class IOCommandList {
  friend struct IOHandler;
  std::vector<IOCmd> cmds;
  std::vector<IOCallBack> callbacks;
  std::vector<file_handle> files;

public:
  void CopyFrom(const FileDesc &src, const RawDataDesc &dst) {
    cmds.push_back({src, dst, 0});
  }
  void CopyFrom(const RawDataDesc &src, const FileDesc &dst) {
    cmds.push_back({src, dst, 0});
  }
  void CopyFrom(const FileDesc &src, const FileDesc &dst) {
    cmds.push_back({src, dst, 0});
  }
  void AddCallback(IOCallBack &&callback) {
    callbacks.push_back(std::move(callback));
  }

  file_handle ResolveFileHandle(const std::filesystem::path &path) {
    assert(std::filesystem::exists(path) && "File does not exist");
    file_handle handle;
    char *path_str = new char[path.string().size() + 1];
    std::strcpy(path_str, path.string().c_str());
    handle.file = path_str;
    handle.length = path.string().size();
    files.push_back(handle);
    return handle;
  }
};

}; // namespace John
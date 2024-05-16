#include "spdlog/spdlog.h"
#include <atomic>
#include <coroutine>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <span>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

struct file_handle {
  void *file = nullptr;
  size_t length = 0;
};
template <typename TFunc> struct Disposer {
  TFunc func;
  Disposer(TFunc &&func) : func(std::move(func)) {}
  ~Disposer() { func(); }
};
template <typename TFunc> Disposer<TFunc> OnExitScope(TFunc &&func) {
  return Disposer<TFunc>(std::move(func));
}
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

class IOLooper {
  std::jthread thread;
  std::mutex mutex;
  std::vector<std::function<void(void)>> requests;
  bool enabled = true;

public:
  IOLooper() {
    thread = std::jthread([this]() { _WorkLoop(); });
  }
  ~IOLooper() {}

  static void Init() { IOLooper::Get(); }
  static void Dispose() { IOLooper::Get().enabled = false; }
  static void EnqueueRequest(file_handle handle, size_t file_offset, void *ptr,
                             size_t len) {
    IOLooper::Get()._EnqueueRequest(handle, file_offset, ptr, len);
  }
  static void EnqueueRequest(const void *ptr, size_t len, file_handle handle,
                             size_t file_offset) {
    IOLooper::Get()._EnqueueRequest(ptr, len, handle, file_offset);
  }
  static void EnqueueSignal(Event *event_handle, uint64_t timeline) {
    IOLooper::Get()._EnqueueSignal(event_handle, timeline);
  }
  static void EnqueueRequest(file_handle handle, size_t offset, size_t src_size,
                             file_handle dst_handle, size_t dst_offset,
                             size_t dst_size) {
    IOLooper::Get()._EnqueueRequest(handle, offset, src_size, dst_handle,
                                    dst_offset, dst_size);
  }

private:
  static IOLooper &Get() {
    static IOLooper looper;
    return looper;
  }
  void _EnqueueRequest(file_handle handle, size_t file_offset, void *ptr,
                       size_t len) {
    std::lock_guard<std::mutex> lk(mutex);
    requests.push_back([=]() {
      auto result_handle = std::fopen((const char *)handle.file, "r");
      if (!result_handle) {
        SPDLOG_ERROR("Failed to open file {}", (const char *)handle.file);
        return;
      }
      std::fseek(result_handle, file_offset, SEEK_SET);
      std::fread(ptr, sizeof(std::byte), len, result_handle);
      std::fclose(result_handle);
    });
  }

  void _EnqueueRequest(const void *ptr, size_t len, file_handle handle,
                       size_t file_offset) {
    std::lock_guard<std::mutex> lk(mutex);
    requests.push_back([=]() {
      auto result_handle = std::fopen((const char *)handle.file, "r+");
      if (!result_handle) {
        SPDLOG_ERROR("Failed to open file {}", (const char *)handle.file);
        return;
      }
      std::fseek(result_handle, file_offset, SEEK_SET);
      std::fwrite(ptr, sizeof(std::byte), len, result_handle);
      std::fclose(result_handle);
    });
  }

  void _EnqueueRequest(file_handle handle, size_t offset, size_t src_size,
                       file_handle in_dst_handle, size_t dst_offset,
                       size_t dst_size) {
    std::lock_guard<std::mutex> lk(mutex);
    requests.push_back([=]() {
      auto src_handle = std::fopen((const char *)handle.file, "r");
      auto dst_handle = std::fopen((const char *)in_dst_handle.file, "r+");
      if (!src_handle || !dst_handle) {
        SPDLOG_ERROR("Failed to open file {}", (const char *)handle.file);
        return;
      }
      std::fseek(src_handle, offset, SEEK_SET);
      std::fseek(dst_handle, dst_offset, SEEK_SET);
      // use fixed size buffer for now
      char buffer[4096];
      size_t read_size = 0;
      while (read_size < src_size) {
        size_t to_read = std::min(sizeof(buffer), src_size - read_size);
        size_t read = std::fread(buffer, 1, to_read, src_handle);
        if (read == 0) {
          break;
        }
        std::fwrite(buffer, 1, read, dst_handle);
        read_size += read;
      }
      std::fclose(src_handle);
      std::fclose(dst_handle);
    });
  }
  void _EnqueueSignal(Event *event_handle, uint64_t timeline) {
    std::lock_guard<std::mutex> lk(mutex);
    requests.push_back([=, this]() { event_handle->Signal(timeline); });
  }
  void _WorkLoop() {
    SPDLOG_INFO("IOLooper started");
    while (enabled) {
      std::vector<std::function<void(void)>> requests_copy;
      {
        std::lock_guard<std::mutex> lk(mutex);
        requests_copy = std::move(requests);
      }
      if (requests_copy.empty()) {
        std::this_thread::yield();
      }
      for (auto &request : requests_copy) {
        request();
      }
    }
    SPDLOG_INFO("IOLooper exited");
  }
};

struct IOService {
  static void Init();
  static void Dispose();

  static uint64_t Execute(class IOCommandList &cmd_list);
  static void Sync(uint64_t time_stamp);
  struct Impl;
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

struct Promise {
  std::promise<uint32_t> status;
  void Sync() { status.get_future().get(); }
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
struct IOCommandListHolder {
  std::vector<IOCmd> cmds;
  std::vector<IOCallBack> callbacks;
  std::vector<file_handle> files;
  uint64_t time_stamp;
};

struct IOHandler {
  struct CallBacks {
    std::vector<IOCallBack> callbacks;
    std::vector<file_handle> files;
    uint64_t time_stamp;
  };
  uint64_t time_stamp;
  std::queue<IOCommandListHolder> cmd_batches;
  std::mutex mutex;
  Event event;
  uint64_t EnqueueCmds(IOCommandList &cmd_list) {
    if (cmd_list.cmds.empty()) {
      return time_stamp;
    }
    {
      std::unique_lock<std::mutex> lk(mutex);
      cmd_batches.emplace(std::move(cmd_list.cmds),
                          std::move(cmd_list.callbacks),
                          std::move(cmd_list.files), ++time_stamp);
    }
    return time_stamp;
  }
  std::queue<CallBacks> _callbacks;
  void Tick() {
    IOCommandListHolder cmds_batch;
    bool has_cmds = false;
    {
      std::unique_lock<std::mutex> lk(mutex);
      if (!cmd_batches.empty()) {
        has_cmds = true;
        cmds_batch = std::move(cmd_batches.front());
        cmd_batches.pop();
      }
    }
    if (has_cmds) {
      AsyncExecuteCmds(cmds_batch);
    }
    if (!_callbacks.empty()) {
      auto &first = _callbacks.front();
      if (event.IsSignaled(first.time_stamp)) {
        for (auto &callback : first.callbacks) {
          callback();
        }
        for (auto &file : first.files) {
          delete[] file.file;
        }
        _callbacks.pop();
        ;
      }
    } else {
      std::this_thread::yield();
    }
  }

  void Join() {
    Tick();
    while (!_callbacks.empty()) {
      auto &first = _callbacks.front();
      event.Wait(first.time_stamp);
      for (auto &callback : first.callbacks) {
        callback();
      }
      for (auto &file : first.files) {
        delete[] file.file;
      }
      _callbacks.pop();
    }
  }

private:
  void AsyncExecuteCmds(IOCommandListHolder &cmd_holder) {
    auto &&cmds = std::move(cmd_holder.cmds);
    auto &&callbacks = std::move(cmd_holder.callbacks);
    auto &&files = std::move(cmd_holder.files);
    bool has_commands = false;

    if (cmds.empty()) {
      return;
    }
    auto exit_func = OnExitScope([&]() {
      _callbacks.push(
          {std::move(callbacks), std::move(files), cmd_holder.time_stamp});
    });
    // iterate over commands
    for (auto &cmd : cmds) {
      has_commands = true;
      std::visit(
          [&](auto &&src, auto &&dst) {
            if constexpr (std::is_same_v<std::decay_t<decltype(src)>,
                                         FileDesc>) {
              if constexpr (std::is_same_v<std::decay_t<decltype(dst)>,
                                           FileDesc>) {
                IOLooper::EnqueueRequest(src.handle, src.offset, src.size,
                                         dst.handle, dst.offset, dst.size);
              } else {
                IOLooper::EnqueueRequest(src.handle, src.offset,
                                         dst.data.data(), dst.data.size());
              }
            } else {
              if constexpr (std::is_same_v<std::decay_t<decltype(dst)>,
                                           FileDesc>) {
                IOLooper::EnqueueRequest(src.data.data(), src.data.size(),
                                         dst.handle, dst.offset);
              } else {
                SPDLOG_ERROR("Invalid command");
              }
            }
          },
          cmd.src, cmd.dst);
    }

    IOLooper::EnqueueSignal(&event, cmd_holder.time_stamp);
  }
};
struct IOService::Impl {
  std::jthread *thread;
  IOHandler handler;
  bool requested_exit = false;
  static IOService::Impl &Get() {
    static IOService::Impl impl;
    return impl;
  }

  using time_stamp = uint32_t;
  void WorkLoop() {
    while (!requested_exit) {
      handler.Tick();
    }
    handler.Join();
  }
  Impl() {
    IOLooper::Init();
    thread = new std::jthread([this]() { WorkLoop(); });
  }
  void Dispose() {
    requested_exit = true;
    delete thread;
    IOLooper::Dispose();
  }
  void Sync(uint64_t time_stamp) { handler.event.Wait(time_stamp); }
};

void IOService::Init() { IOService::Impl::Get(); }
void IOService::Dispose() { IOService::Impl::Get().Dispose(); }
void IOService::Sync(uint64_t time_stamp) {
  IOService::Impl::Get().Sync(time_stamp);
}
uint64_t IOService::Execute(IOCommandList &cmd_list) {
  return IOService::Impl::Get().handler.EnqueueCmds(cmd_list);
}
int main(const int argc, const char **argv) {
  IOService::Init();
  IOCommandList cmd_list;
  std::filesystem::path src_path = argv[0];
  if (src_path.has_filename()) {
    src_path = src_path.parent_path().parent_path().parent_path() / "src.txt";
  }

  auto src = cmd_list.ResolveFileHandle(src_path);
  auto dst = cmd_list.ResolveFileHandle(src_path.parent_path() / "dst.txt");

  std::string_view data = "Hello World";
  RawDataDesc data_desc = {
      std::span<uint8_t>((uint8_t *)data.data(), data.size())};
  cmd_list.CopyFrom(FileDesc{src, 0, 10}, FileDesc{dst, 0, 10});
  cmd_list.CopyFrom(data_desc, FileDesc{dst, 10, 10});
  uint64_t ev = IOService::Execute(cmd_list);
  IOService::Sync(ev);
  IOService::Dispose();
  return 0;
}
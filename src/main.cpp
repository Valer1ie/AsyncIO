#include "IOService.h"
#include <filesystem>
int main(const int argc, const char **argv) {
  using namespace John;
  IOService::Init();
  OnExitScope([]() { IOService::Dispose(); });
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
  return 0;
}
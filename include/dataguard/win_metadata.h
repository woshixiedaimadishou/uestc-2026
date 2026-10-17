#pragma once

#include <filesystem>
#include <string>

#include "dataguard/file_entry.h"

namespace dataguard {

// 读取文件元数据并填充 FileEntry：
//   - kind：根据 symlink_status 判断（符号链接不跟随）
//   - size：普通文件大小；目录/链接为 0
//   - mtime/ctime/atime：Unix 秒
//   - attrs：Windows 文件属性位掩码
//   - owner：属主用户名（SID → 账户名；失败留空）
//   - link_target_utf8：符号链接目标
//   - file_id / hard_link_count：用于硬链接识别
bool readFileMetadata(const std::filesystem::path& path,
                      FileEntry& entry,
                      std::string* error = nullptr);

// 还原时写回元数据：SetFileTime + SetFileAttributes。
// 属主设置需要特权，失败仅记录错误字符串，不视为致命。
bool applyFileMetadata(const std::filesystem::path& path,
                       const FileEntry& entry,
                       std::string* error = nullptr);

// 唯一文件 ID：volume serial + file index 经 FNV-1a 混合；失败返回 0。
uint64_t getFileId(const std::filesystem::path& path);

}  // namespace dataguard

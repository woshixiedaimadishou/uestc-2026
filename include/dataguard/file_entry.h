#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace dataguard {

// 条目类型：普通文件 / 目录 / 符号链接 / 硬链接引用 / 其他（设备、管道等）
enum class FileKind : uint8_t {
    File = 0,
    Directory = 1,
    Symlink = 2,
    Hardlink = 3,
    Other = 4
};

// 文件条目：扫描结果与档案条目共用的数据结构
struct FileEntry {
    std::filesystem::path abs_path;      // 扫描时的绝对路径（运行时使用，不写档案）
    std::string rel_path_utf8;           // 档案内相对路径，UTF-8，目录分隔符为 '/'
    FileKind kind = FileKind::File;
    uint64_t size = 0;                   // 原始文件大小（字节）；目录/链接为 0
    int64_t mtime = 0;                   // 修改时间（Unix 秒）
    int64_t ctime = 0;                   // 创建时间（Unix 秒）
    int64_t atime = 0;                   // 访问时间（Unix 秒）
    uint32_t attrs = 0;                  // Windows FILE_ATTRIBUTE_* 位掩码
    std::string owner;                   // 属主用户名（SID 解析失败则为空）
    std::string link_target_utf8;        // 符号链接目标（UTF-8）
    uint64_t file_id = 0;                // volume serial + file index 的 FNV-1a 哈希
    uint64_t hard_link_count = 0;        // 硬链接计数（>1 表示存在硬链接）
};

}  // namespace dataguard

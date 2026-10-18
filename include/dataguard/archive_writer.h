#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "dataguard/archive_format.h"
#include "dataguard/file_entry.h"

namespace dataguard {

// 档案选项：v0.2 支持无压缩、无加密路径；压缩/加密分别在 v0.3/v0.4 接入
struct ArchiveOptions {
    bool compress = false;
    bool encrypt = false;
    std::string password;
};

// 档案写入器：
//   open()  -> 记录目标路径与选项（不立即写文件）
//   add()   -> 读入文件内容、计算 CRC、登记条目
//   close() -> 一次性写出 FixedHeader + Entry Table + Data Region
//
// 实现说明：条目内容保存在内存中，close 时统一落盘。
// 课程规模（单文件不超过 1GB、总量适中）下该实现简单可靠；
// 超限文件由 BackupManager 跳过并告警。
class ArchiveWriter {
public:
    ArchiveWriter() = default;
    ~ArchiveWriter();

    ArchiveWriter(const ArchiveWriter&) = delete;
    ArchiveWriter& operator=(const ArchiveWriter&) = delete;

    bool open(const std::filesystem::path& path, const ArchiveOptions& options);
    bool add(const FileEntry& entry);
    bool close();

    const std::string& last_error() const { return last_error_; }
    size_t entryCount() const { return entries_.size(); }

private:
    static void packEntry(const FileEntry& entry, std::vector<uint8_t>& out);

    std::filesystem::path path_;
    ArchiveOptions options_;
    bool opened_ = false;
    bool closed_ = false;
    std::vector<FileEntry> entries_;
    std::vector<std::vector<uint8_t>> datas_;
    uint64_t total_data_size_ = 0;
    std::string last_error_;
};

}  // namespace dataguard

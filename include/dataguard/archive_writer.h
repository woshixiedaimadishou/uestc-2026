#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dataguard/archive_format.h"
#include "dataguard/compressor.h"
#include "dataguard/file_entry.h"

namespace dataguard {

// 档案选项：压缩方案 + 是否加密
struct ArchiveOptions {
    Codec codec = Codec::None;
    bool encrypt = false;
    std::string password;

    // 兼容旧代码：true 等价于 Codec::Deflate
    bool compress = false;
};

// 已准备好的条目：文件内容已读入、CRC 已算、按需压缩完成。
// 把「读文件 + 压缩」与「按顺序登记」拆开，前者可以多线程并行。
struct PreparedEntry {
    FileEntry entry;
    std::vector<uint8_t> data;  // 已压缩（若启用压缩）的存储字节
    std::string error;
    bool ok = false;
};

// 档案写入器：
//   open()        -> 记录目标路径与选项（不立即写文件）
//   prepare()     -> 读入文件内容、计算 CRC、按需压缩（纯函数，可并行调用）
//   addPrepared() -> 按顺序登记条目并累计偏移（必须单线程按序调用）
//   add()         -> prepare + addPrepared（兼容旧用法）
//   close()       -> 写出 FixedHeader + Entry Table + Data Region
//
// 实现说明：条目内容保存在内存中，close 时流式落盘。
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
    bool addPrepared(PreparedEntry&& prepared);
    bool close();

    // 读文件 + 计算校验 + 压缩；不修改写入器状态，可多线程调用
    static bool prepare(const FileEntry& entry, const ArchiveOptions& options,
                        PreparedEntry& out);

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
    std::vector<uint8_t> iv_;
    std::unordered_map<uint64_t, std::string> seen_file_ids_;  // 硬链接去重：file_id → 首个条目路径
    std::string last_error_;
};

}  // namespace dataguard

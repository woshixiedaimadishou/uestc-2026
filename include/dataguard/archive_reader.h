#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "dataguard/archive_format.h"
#include "dataguard/file_entry.h"

namespace dataguard {

struct ArchiveOptions;  // 定义见 archive_writer.h

// 档案读取器：
//   open()      -> 读取并校验 FixedHeader / Entry Table
//   entries()   -> 档案内条目列表
//   extract()   -> 解出单个条目到目标目录（自动建目录、恢复元数据、重建链接）
//   verify()    -> 逐文件重算 CRC，检出损坏条目
class ArchiveReader {
public:
    ArchiveReader() = default;
    ~ArchiveReader();

    ArchiveReader(const ArchiveReader&) = delete;
    ArchiveReader& operator=(const ArchiveReader&) = delete;

    bool open(const std::filesystem::path& path, const std::string& password = "");
    bool extract(const FileEntry& entry, const std::filesystem::path& dest);
    bool extractAll(const std::filesystem::path& dest);
    bool verify(std::vector<std::string>* bad_files = nullptr);

    const std::vector<FileEntry>& entries() const { return entries_; }
    bool compressed() const { return compressed_; }
    bool encrypted() const { return encrypted_; }
    const std::string& last_error() const { return last_error_; }

private:
    bool readEntryTable(size_t table_offset, size_t data_offset, uint32_t count);
    bool readEntryData(const FileEntry& entry, std::vector<uint8_t>& out) const;

    std::vector<uint8_t> file_data_;
    std::vector<FileEntry> entries_;
    size_t data_offset_ = 0;
    bool compressed_ = false;
    bool encrypted_ = false;
    std::string last_error_;
};

}  // namespace dataguard

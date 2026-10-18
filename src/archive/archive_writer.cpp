#include "dataguard/archive_writer.h"

#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "dataguard/crc32.h"
#include "dataguard/path_utils.h"

namespace dataguard {

ArchiveWriter::~ArchiveWriter() {
    if (opened_ && !closed_) {
        close();
    }
}

bool ArchiveWriter::open(const std::filesystem::path& path, const ArchiveOptions& options) {
    if (opened_) {
        last_error_ = "档案写入器已打开，不能重复打开";
        return false;
    }
    if (options.compress) {
        last_error_ = "压缩功能将在 v0.3 提供（当前版本仅支持未压缩档案）";
        return false;
    }
    if (options.encrypt) {
        last_error_ = "加密功能将在 v0.4 提供（当前版本仅支持未加密档案）";
        return false;
    }

    path_ = path;
    options_ = options;
    entries_.clear();
    datas_.clear();
    total_data_size_ = 0;
    opened_ = true;
    closed_ = false;
    last_error_.clear();
    return true;
}

bool ArchiveWriter::add(const FileEntry& entry) {
    if (!opened_ || closed_) {
        last_error_ = "档案未打开";
        return false;
    }

    FileEntry record = entry;
    std::vector<uint8_t> data;

    if (record.kind == FileKind::File) {
        std::ifstream in(record.abs_path, std::ios::binary);
        if (!in) {
            last_error_ = "无法读取文件: " + pathToUtf8(record.abs_path);
            return false;
        }
        data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        record.size = static_cast<uint64_t>(data.size());
        record.checksum = crc32(data.data(), data.size());
    } else {
        // 目录 / 符号链接 / 其他类型：无数据区内容
        if (record.kind == FileKind::Directory) record.size = 0;
        record.checksum = 0;
    }

    record.stored_size = static_cast<uint64_t>(data.size());
    record.offset = total_data_size_;
    total_data_size_ += record.stored_size;

    entries_.push_back(record);
    datas_.push_back(std::move(data));
    return true;
}

void ArchiveWriter::packEntry(const FileEntry& entry, std::vector<uint8_t>& out) {
    appendU32(out, static_cast<uint32_t>(entry.rel_path_utf8.size()));
    out.push_back(static_cast<uint8_t>(entry.kind));
    appendU64(out, entry.size);
    appendU64(out, entry.stored_size);
    appendI64(out, entry.mtime);
    appendI64(out, entry.ctime);
    appendI64(out, entry.atime);
    appendU32(out, entry.attrs);
    appendU32(out, static_cast<uint32_t>(entry.owner.size()));
    appendU32(out, static_cast<uint32_t>(entry.link_target_utf8.size()));
    appendU64(out, entry.file_id);
    appendU32(out, entry.checksum);
    appendU64(out, entry.offset);

    out.insert(out.end(), entry.rel_path_utf8.begin(), entry.rel_path_utf8.end());
    out.insert(out.end(), entry.owner.begin(), entry.owner.end());
    out.insert(out.end(), entry.link_target_utf8.begin(), entry.link_target_utf8.end());
}

bool ArchiveWriter::close() {
    if (!opened_ || closed_) return true;

    // 1. 序列化条目表
    std::vector<uint8_t> table;
    for (const auto& entry : entries_) {
        packEntry(entry, table);
    }

    // 2. 计算各段偏移（iv_len = 0）
    const uint32_t iv_len = 0;
    const uint32_t table_offset = kTableOffsetBase + iv_len + kTableOffsetTail;
    const uint32_t data_offset = static_cast<uint32_t>(table_offset + table.size());
    const uint16_t flags = 0;  // 未压缩、未加密

    // 3. 构造 FixedHeader
    std::vector<uint8_t> header;
    header.reserve(kHeaderFixedSize);
    appendU32(header, kMagic);
    appendU16(header, kVersion);
    appendU16(header, flags);
    header.push_back(kCompressorNone);
    header.push_back(kCipherNone);
    appendU32(header, static_cast<uint32_t>(entries_.size()));
    appendU32(header, table_offset);
    appendU32(header, data_offset);

    const uint32_t header_crc = crc32(header.data(), header.size());  // 覆盖 [0..22)
    appendU32(header, header_crc);
    appendU32(header, iv_len);
    // iv（无）
    appendU32(header, 0u);  // password_check
    appendU32(header, crc32(table.data(), table.size()));  // table_crc

    // 4. 落盘：header + table + data
    std::ofstream out(path_, std::ios::binary | std::ios::trunc);
    if (!out) {
        last_error_ = "无法创建档案文件: " + pathToUtf8(path_);
        return false;
    }
    out.write(reinterpret_cast<const char*>(header.data()),
              static_cast<std::streamsize>(header.size()));
    out.write(reinterpret_cast<const char*>(table.data()),
              static_cast<std::streamsize>(table.size()));
    for (const auto& data : datas_) {
        if (!data.empty()) {
            out.write(reinterpret_cast<const char*>(data.data()),
                      static_cast<std::streamsize>(data.size()));
        }
    }
    out.flush();
    if (!out) {
        last_error_ = "写档案失败（磁盘空间不足？）: " + pathToUtf8(path_);
        return false;
    }

    closed_ = true;
    return true;
}

}  // namespace dataguard

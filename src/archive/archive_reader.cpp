#include "dataguard/archive_reader.h"

#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#include "dataguard/crc32.h"
#include "dataguard/path_utils.h"
#include "dataguard/win_metadata.h"

namespace dataguard {
namespace {

constexpr size_t kBaseHeaderBytes = 30;  // [0..30)：magic..iv_len

}  // namespace

ArchiveReader::~ArchiveReader() = default;

bool ArchiveReader::open(const std::filesystem::path& path, const std::string& password) {
    (void)password;  // v0.4 接入加密校验；当前版本不支持加密档案

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        last_error_ = "无法打开档案文件: " + pathToUtf8(path);
        return false;
    }
    file_data_.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());

    if (file_data_.size() < kBaseHeaderBytes) {
        last_error_ = "档案文件过短，不是有效的 BKPK 档案";
        return false;
    }

    const uint8_t* base = file_data_.data();
    if (readU32(base) != kMagic) {
        last_error_ = "档案魔数不匹配（不是 BKPK 档案）";
        return false;
    }
    const uint16_t version = readU16(base + 4);
    if (version != kVersion) {
        last_error_ = "不支持的档案版本: " + std::to_string(version);
        return false;
    }

    const uint16_t flags = readU16(base + 6);
    const uint8_t compressor_id = base[8];
    const uint8_t cipher_id = base[9];
    const uint32_t entry_count = readU32(base + 10);
    const uint32_t table_offset = readU32(base + 14);
    const uint32_t data_offset = readU32(base + 18);
    const uint32_t header_crc = readU32(base + 22);
    const uint32_t iv_len = readU32(base + 26);

    if (crc32(base, kHeaderCrcCover) != header_crc) {
        last_error_ = "档案头校验失败（文件可能损坏）";
        return false;
    }

    compressed_ = (flags & kFlagCompressed) != 0;
    encrypted_ = (flags & kFlagEncrypted) != 0;

    if (compressor_id != kCompressorNone) {
        last_error_ = "档案使用压缩（id=" + std::to_string(compressor_id) +
                      "），当前版本（v0.2）尚不支持解压";
        return false;
    }
    if (cipher_id != kCipherNone) {
        last_error_ = "档案使用加密（id=" + std::to_string(cipher_id) +
                      "），当前版本（v0.2）尚不支持解密";
        return false;
    }
    if (iv_len > 4096 || static_cast<size_t>(iv_len) + kBaseHeaderBytes + 8 > file_data_.size()) {
        last_error_ = "档案头长度字段非法";
        return false;
    }

    const size_t table_crc_pos = static_cast<size_t>(kBaseHeaderBytes) + iv_len + 4;
    if (table_crc_pos + 4 > file_data_.size()) {
        last_error_ = "档案头不完整";
        return false;
    }
    const uint32_t table_crc = readU32(file_data_.data() + table_crc_pos);

    if (table_offset > data_offset || data_offset > file_data_.size()) {
        last_error_ = "档案偏移字段非法";
        return false;
    }

    std::vector<uint8_t> table(file_data_.begin() + table_offset,
                               file_data_.begin() + data_offset);
    if (crc32(table.data(), table.size()) != table_crc) {
        last_error_ = "条目表校验失败（文件可能损坏）";
        return false;
    }

    data_offset_ = data_offset;
    return readEntryTable(table_offset, data_offset, entry_count);
}

bool ArchiveReader::readEntryTable(size_t table_offset, size_t data_offset, uint32_t count) {
    entries_.clear();
    size_t pos = table_offset;
    for (uint32_t i = 0; i < count; ++i) {
        if (pos + 4 > data_offset) {
            last_error_ = "条目表越界（条目数声明与实际不符）";
            return false;
        }
        const uint32_t name_len = readU32(file_data_.data() + pos);
        pos += 4;
        const size_t fixed = 1 + 8 + 8 + 8 + 8 + 8 + 4 + 4 + 4 + 8 + 4 + 8;  // 73 字节
        if (pos + fixed + name_len > data_offset) {
            last_error_ = "条目记录越界";
            return false;
        }

        FileEntry entry;
        entry.kind = static_cast<FileKind>(file_data_[pos]);
        pos += 1;
        entry.size = readU64(file_data_.data() + pos);
        pos += 8;
        entry.stored_size = readU64(file_data_.data() + pos);
        pos += 8;
        entry.mtime = readI64(file_data_.data() + pos);
        pos += 8;
        entry.ctime = readI64(file_data_.data() + pos);
        pos += 8;
        entry.atime = readI64(file_data_.data() + pos);
        pos += 8;
        entry.attrs = readU32(file_data_.data() + pos);
        pos += 4;
        const uint32_t owner_len = readU32(file_data_.data() + pos);
        pos += 4;
        const uint32_t link_target_len = readU32(file_data_.data() + pos);
        pos += 4;
        entry.file_id = readU64(file_data_.data() + pos);
        pos += 8;
        entry.checksum = readU32(file_data_.data() + pos);
        pos += 4;
        entry.offset = readU64(file_data_.data() + pos);
        pos += 8;

        if (pos + name_len + owner_len + link_target_len > data_offset) {
            last_error_ = "条目字符串越界";
            return false;
        }
        entry.rel_path_utf8.assign(reinterpret_cast<const char*>(file_data_.data() + pos), name_len);
        pos += name_len;
        entry.owner.assign(reinterpret_cast<const char*>(file_data_.data() + pos), owner_len);
        pos += owner_len;
        entry.link_target_utf8.assign(
            reinterpret_cast<const char*>(file_data_.data() + pos), link_target_len);
        pos += link_target_len;

        if (entry.offset + entry.stored_size > file_data_.size() - data_offset_) {
            last_error_ = "数据区越界: " + entry.rel_path_utf8;
            return false;
        }
        entries_.push_back(entry);
    }
    return true;
}

bool ArchiveReader::readEntryData(const FileEntry& entry, std::vector<uint8_t>& out) const {
    if (entry.stored_size == 0) {
        out.clear();
        return true;
    }
    const size_t begin = data_offset_ + static_cast<size_t>(entry.offset);
    const size_t end = begin + static_cast<size_t>(entry.stored_size);
    if (end > file_data_.size()) return false;
    out.assign(file_data_.begin() + begin, file_data_.begin() + end);
    return true;
}

bool ArchiveReader::extract(const FileEntry& entry, const std::filesystem::path& dest) {
    if (entry.rel_path_utf8.empty()) {
        last_error_ = "条目路径为空";
        return false;
    }
    const std::filesystem::path target = dest / utf8ToPath(entry.rel_path_utf8);
    std::error_code ec;

    if (entry.kind == FileKind::Directory) {
        std::filesystem::create_directories(target, ec);
        if (ec) {
            last_error_ = "创建目录失败: " + pathToUtf8(target);
            return false;
        }
        std::string warn;
        applyFileMetadata(target, entry, &warn);  // 元数据失败不视为致命错误
        return true;
    }

    if (entry.kind == FileKind::Symlink) {
        std::filesystem::create_directories(target.parent_path(), ec);
        if (std::filesystem::exists(target, ec)) {
            std::filesystem::remove(target, ec);
        }
        std::filesystem::create_symlink(utf8ToPath(entry.link_target_utf8), target, ec);
        if (ec) {
            last_error_ = "创建符号链接失败（可能需要权限）: " + pathToUtf8(target);
            return false;
        }
        return true;
    }

    if (entry.kind == FileKind::File) {
        std::vector<uint8_t> data;
        if (!readEntryData(entry, data)) {
            last_error_ = "读取条目数据失败: " + entry.rel_path_utf8;
            return false;
        }
        if (data.size() != entry.size) {
            last_error_ = "条目长度不一致: " + entry.rel_path_utf8;
            return false;
        }
        if (entry.checksum != 0 && crc32(data.data(), data.size()) != entry.checksum) {
            last_error_ = "条目 CRC 校验失败（数据损坏）: " + entry.rel_path_utf8;
            return false;
        }

        std::filesystem::create_directories(target.parent_path(), ec);
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out) {
            last_error_ = "无法写入文件: " + pathToUtf8(target);
            return false;
        }
        if (!data.empty()) {
            out.write(reinterpret_cast<const char*>(data.data()),
                      static_cast<std::streamsize>(data.size()));
        }
        out.flush();
        if (!out) {
            last_error_ = "写入文件失败: " + pathToUtf8(target);
            return false;
        }
        std::string warn;
        applyFileMetadata(target, entry, &warn);
        return true;
    }

    // FileKind::Other / Hardlink（v0.4 完善）暂不还原
    last_error_ = "跳过不可还原的条目类型: " + entry.rel_path_utf8;
    return false;
}

bool ArchiveReader::extractAll(const std::filesystem::path& dest) {
    std::error_code ec;
    std::filesystem::create_directories(dest, ec);
    if (ec) {
        last_error_ = "无法创建目标目录: " + pathToUtf8(dest);
        return false;
    }
    for (const auto& entry : entries_) {
        if (!extract(entry, dest)) {
            // 单条目失败不中断整体还原，错误保留在 last_error_
            continue;
        }
    }
    return true;
}

bool ArchiveReader::verify(std::vector<std::string>* bad_files) {
    if (bad_files) bad_files->clear();
    bool all_ok = true;
    for (const auto& entry : entries_) {
        if (entry.kind != FileKind::File) continue;

        std::vector<uint8_t> data;
        bool ok = readEntryData(entry, data);
        if (ok) {
            if (data.size() != entry.size) {
                ok = false;
            } else if (entry.checksum != 0 && crc32(data.data(), data.size()) != entry.checksum) {
                ok = false;
            }
        }
        if (!ok) {
            all_ok = false;
            if (bad_files) bad_files->push_back(entry.rel_path_utf8);
        }
    }
    if (!all_ok) last_error_ = "存在损坏条目";
    return all_ok;
}

}  // namespace dataguard

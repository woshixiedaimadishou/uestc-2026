#include "dataguard/archive_writer.h"

#include <array>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "dataguard/cipher.h"
#include "dataguard/crc32.h"
#include "dataguard/deflate.h"
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
    if (options.encrypt && options.password.empty()) {
        last_error_ = "启用加密时必须提供口令";
        return false;
    }

    path_ = path;
    options_ = options;
    entries_.clear();
    datas_.clear();
    total_data_size_ = 0;
    seen_file_ids_.clear();
    iv_.clear();
    if (options_.encrypt) {
        StreamCipher cipher(options_.password);
        iv_ = cipher.iv();  // 生成 16 字节随机 IV
    }
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

    // 硬链接去重：同一 file_id 的后续条目只写引用，不重复存数据
    if (record.kind == FileKind::File && record.hard_link_count > 1 && record.file_id != 0) {
        const auto it = seen_file_ids_.find(record.file_id);
        if (it != seen_file_ids_.end()) {
            record.kind = FileKind::Hardlink;
            record.link_target_utf8 = it->second;  // 指向已存储的首个条目
            record.size = 0;
            record.checksum = 0;
        } else {
            seen_file_ids_[record.file_id] = record.rel_path_utf8;
        }
    }

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
        // 目录 / 符号链接 / 硬链接引用 / 其他类型：无数据区内容
        record.size = 0;
        record.checksum = 0;
    }

    // DEFLATE 压缩（仅对普通文件数据；checksum 始终记录原始数据 CRC）
    if (record.kind == FileKind::File && options_.compress && !data.empty()) {
        DeflateCompressor deflate;
        std::vector<uint8_t> compressed;
        if (!deflate.compress(data, compressed)) {
            last_error_ = "压缩失败: " + record.rel_path_utf8;
            return false;
        }
        data.swap(compressed);
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

    // 2. 计算各段偏移
    const uint32_t iv_len = options_.encrypt ? 16u : 0u;
    const uint32_t table_offset = kTableOffsetBase + iv_len + kTableOffsetTail;
    const uint32_t data_offset = static_cast<uint32_t>(table_offset + table.size());

    uint16_t flags = options_.compress ? kFlagCompressed : static_cast<uint16_t>(0);
    if (options_.encrypt) flags |= kFlagEncrypted;
    const uint8_t compressor_id = options_.compress ? kCompressorDeflate : kCompressorNone;
    const uint8_t cipher_id = options_.encrypt ? kCipherStream : kCipherNone;

    // 3. 组装 body（Entry Table + Data Region）；加密时对整个 body 做流加密
    std::vector<uint8_t> body = table;
    for (const auto& data : datas_) {
        body.insert(body.end(), data.begin(), data.end());
    }

    std::array<uint8_t, 4> password_check{};
    if (options_.encrypt) {
        StreamCipher cipher(options_.password);
        cipher.set_iv(iv_);
        std::vector<uint8_t> encrypted;
        if (!cipher.encrypt(body, encrypted)) {
            last_error_ = "档案加密失败";
            return false;
        }
        body.swap(encrypted);
        password_check = cipher.password_check();
    }

    // 4. 构造 FixedHeader（table_crc 始终基于明文条目表）
    std::vector<uint8_t> header;
    header.reserve(kHeaderFixedSize + iv_len);
    appendU32(header, kMagic);
    appendU16(header, kVersion);
    appendU16(header, flags);
    header.push_back(compressor_id);
    header.push_back(cipher_id);
    appendU32(header, static_cast<uint32_t>(entries_.size()));
    appendU32(header, table_offset);
    appendU32(header, data_offset);

    const uint32_t header_crc = crc32(header.data(), header.size());  // 覆盖 [0..22)
    appendU32(header, header_crc);
    appendU32(header, iv_len);
    if (iv_len > 0) {
        header.insert(header.end(), iv_.begin(), iv_.end());
    }
    for (uint8_t byte : password_check) header.push_back(byte);
    appendU32(header, crc32(table.data(), table.size()));  // table_crc（明文）

    // 5. 落盘：header + body
    std::ofstream out(path_, std::ios::binary | std::ios::trunc);
    if (!out) {
        last_error_ = "无法创建档案文件: " + pathToUtf8(path_);
        return false;
    }
    out.write(reinterpret_cast<const char*>(header.data()),
              static_cast<std::streamsize>(header.size()));
    if (!body.empty()) {
        out.write(reinterpret_cast<const char*>(body.data()),
                  static_cast<std::streamsize>(body.size()));
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

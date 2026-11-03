#include "dataguard/archive_writer.h"

#include <array>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "dataguard/cipher.h"
#include "dataguard/compressor.h"
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
    if (options.encrypt && options.password.empty()) {
        last_error_ = "启用加密时必须提供口令";
        return false;
    }

    path_ = path;
    options_ = options;
    // 兼容旧的 compress 开关
    if (options_.compress && options_.codec == Codec::None) options_.codec = Codec::Deflate;
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

bool ArchiveWriter::prepare(const FileEntry& entry, const ArchiveOptions& options,
                            PreparedEntry& out) {
    out.entry = entry;
    out.data.clear();
    out.error.clear();
    out.ok = false;

    const Codec codec = (options.compress && options.codec == Codec::None)
                            ? Codec::Deflate
                            : options.codec;

    if (out.entry.kind == FileKind::File) {
        std::ifstream in(out.entry.abs_path, std::ios::binary);
        if (!in) {
            out.error = "无法读取文件: " + pathToUtf8(out.entry.abs_path);
            return false;
        }
        out.data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        out.entry.size = static_cast<uint64_t>(out.data.size());
        out.entry.checksum = crc32(out.data.data(), out.data.size());

        // 压缩（checksum 始终记录原始数据 CRC；stored_size 在登记时回填）
        if (codec != Codec::None && !out.data.empty()) {
            const auto compressor = makeCompressor(codec);
            if (!compressor) {
                out.error = "不支持的压缩方案";
                return false;
            }
            std::vector<uint8_t> compressed;
            if (!compressor->compress(out.data, compressed)) {
                out.error = "压缩失败: " + out.entry.rel_path_utf8;
                return false;
            }
            out.data.swap(compressed);
        }
    } else {
        // 目录 / 符号链接 / 硬链接引用 / 其他类型：无数据区内容
        out.entry.size = 0;
        out.entry.checksum = 0;
    }

    out.ok = true;
    return true;
}

bool ArchiveWriter::add(const FileEntry& entry) {
    if (!opened_ || closed_) {
        last_error_ = "档案未打开";
        return false;
    }
    PreparedEntry prepared;
    if (!prepare(entry, options_, prepared)) {
        last_error_ = prepared.error;
        return false;
    }
    return addPrepared(std::move(prepared));
}

bool ArchiveWriter::addPrepared(PreparedEntry&& prepared) {
    if (!opened_ || closed_) {
        last_error_ = "档案未打开";
        return false;
    }
    if (!prepared.ok) {
        last_error_ = prepared.error.empty() ? "条目准备失败" : prepared.error;
        return false;
    }

    FileEntry record = std::move(prepared.entry);
    std::vector<uint8_t> data = std::move(prepared.data);

    // 硬链接去重：同一 file_id 的后续条目只写引用，不重复存数据
    if (record.kind == FileKind::File && record.hard_link_count > 1 && record.file_id != 0) {
        const auto it = seen_file_ids_.find(record.file_id);
        if (it != seen_file_ids_.end()) {
            record.kind = FileKind::Hardlink;
            record.link_target_utf8 = it->second;  // 指向已存储的首个条目
            record.size = 0;
            record.checksum = 0;
            data.clear();
        } else {
            seen_file_ids_[record.file_id] = record.rel_path_utf8;
        }
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

    uint16_t flags = (options_.codec != Codec::None) ? kFlagCompressed : static_cast<uint16_t>(0);
    if (options_.encrypt) flags |= kFlagEncrypted;
    const uint8_t compressor_id = codecId(options_.codec);
    const uint8_t cipher_id = options_.encrypt ? kCipherStream : kCipherNone;

    // 3. 口令校验值（不动 body，加密在落盘时按块流式做）
    std::array<uint8_t, 4> password_check{};
    if (options_.encrypt) {
        StreamCipher cipher(options_.password);
        cipher.set_iv(iv_);
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

    // 5. 落盘：header + （Entry Table + Data Region，加密时逐块加密写出）
    //    这里不再把整包 body 拼成一个缓冲区，避免对全量数据再做一次拷贝。

    std::ofstream out(path_, std::ios::binary | std::ios::trunc);
    if (!out) {
        last_error_ = "无法创建档案文件: " + pathToUtf8(path_);
        return false;
    }
    out.write(reinterpret_cast<const char*>(header.data()),
              static_cast<std::streamsize>(header.size()));

    if (options_.encrypt) {
        StreamCipherStream stream(options_.password, iv_);
        std::vector<uint8_t> chunk;
        auto writeEncrypted = [&](const uint8_t* data, size_t size) {
            if (size == 0) return;
            stream.process(data, size, chunk);
            out.write(reinterpret_cast<const char*>(chunk.data()),
                      static_cast<std::streamsize>(chunk.size()));
        };
        writeEncrypted(table.data(), table.size());
        for (const auto& data : datas_) writeEncrypted(data.data(), data.size());
    } else {
        if (!table.empty()) {
            out.write(reinterpret_cast<const char*>(table.data()),
                      static_cast<std::streamsize>(table.size()));
        }
        for (const auto& data : datas_) {
            if (!data.empty()) {
                out.write(reinterpret_cast<const char*>(data.data()),
                          static_cast<std::streamsize>(data.size()));
            }
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

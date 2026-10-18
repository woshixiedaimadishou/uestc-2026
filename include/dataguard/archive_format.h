#pragma once

#include <cstdint>
#include <vector>

namespace dataguard {

// BKPK v1 档案格式常量（小端序）
constexpr uint32_t kMagic = 0x4B504B42u;      // 'BKPB'
constexpr uint16_t kVersion = 1;

constexpr uint16_t kFlagCompressed = 0x0001;
constexpr uint16_t kFlagEncrypted = 0x0002;

constexpr uint8_t kCompressorNone = 0;
constexpr uint8_t kCompressorDeflate = 1;

constexpr uint8_t kCipherNone = 0;
constexpr uint8_t kCipherStream = 1;

// FixedHeader 布局（iv_len == 0 时总长 38 字节）：
//   [0..22)   magic(4) version(2) flags(2) compressor(1) cipher(1)
//             entry_count(4) table_offset(4) data_offset(4)
//   [22..26)  header_crc   覆盖 [0..22)
//   [26..30)  iv_len
//   [30..30+iv_len) iv
//   [30+iv_len .. 34+iv_len) password_check
//   [34+iv_len .. 38+iv_len) table_crc   覆盖 Entry Table
constexpr uint32_t kHeaderCrcCover = 22;
constexpr uint32_t kHeaderFixedSize = 38;

// 条目表偏移公式：table_offset = 34 + iv_len + 4
constexpr uint32_t kTableOffsetBase = 34;
constexpr uint32_t kTableOffsetTail = 4;

constexpr uint32_t kIoBlockSize = 65536;

// ---------------- 小端序列化辅助 ----------------
inline void appendU16(std::vector<uint8_t>& buf, uint16_t value) {
    buf.push_back(static_cast<uint8_t>(value & 0xFF));
    buf.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

inline void appendU32(std::vector<uint8_t>& buf, uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        buf.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
    }
}

inline void appendU64(std::vector<uint8_t>& buf, uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        buf.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
    }
}

inline void appendI64(std::vector<uint8_t>& buf, int64_t value) {
    appendU64(buf, static_cast<uint64_t>(value));
}

inline uint16_t readU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t readU32(const uint8_t* p) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(p[i]) << (8 * i);
    return v;
}

inline uint64_t readU64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(p[i]) << (8 * i);
    return v;
}

inline int64_t readI64(const uint8_t* p) {
    return static_cast<int64_t>(readU64(p));
}

}  // namespace dataguard

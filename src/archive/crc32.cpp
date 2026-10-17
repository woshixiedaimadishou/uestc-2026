#include "dataguard/crc32.h"

namespace dataguard {
namespace {

// 延迟初始化的 CRC-32 查找表（多项式 0xEDB88320）
const uint32_t* crcTable() {
    static uint32_t table[256];
    static bool initialized = false;
    if (!initialized) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1U) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        initialized = true;
    }
    return table;
}

}  // namespace

uint32_t crc32(const uint8_t* data, size_t len, uint32_t seed) {
    const uint32_t* table = crcTable();
    uint32_t c = seed ^ 0xFFFFFFFFU;
    for (size_t i = 0; i < len; ++i) {
        c = table[(c ^ data[i]) & 0xFFU] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFU;
}

}  // namespace dataguard

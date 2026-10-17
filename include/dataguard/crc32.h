#pragma once

#include <cstddef>
#include <cstdint>

namespace dataguard {

// 标准 CRC-32：多项式 0xEDB88320，查表法，初值 0xFFFFFFFF，结果按位取反。
// 自检向量：crc32("123456789", 9) == 0xCBF43926
uint32_t crc32(const uint8_t* data, size_t len, uint32_t seed = 0);

}  // namespace dataguard

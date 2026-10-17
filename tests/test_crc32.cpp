#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "dataguard/crc32.h"

namespace {

uint32_t crcOf(const std::string& text) {
    return dataguard::crc32(reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

}  // namespace

// 用例 1：CRC-32 标准自检向量（RFC 1952 / zlib 通用）
TEST(Crc32Test, StandardVector) {
    EXPECT_EQ(crcOf("123456789"), 0xCBF43926u);
}

// 用例 2：空输入的 CRC 为 0
TEST(Crc32Test, EmptyInputIsZero) {
    EXPECT_EQ(crcOf(""), 0u);
}

// 用例 3：不同内容得到不同 CRC
TEST(Crc32Test, DifferentContentDiffers) {
    EXPECT_NE(crcOf("abc"), crcOf("abd"));
    EXPECT_NE(crcOf("a"), crcOf("b"));
}

// 用例 4：长度不同则结果不同
TEST(Crc32Test, DifferentLengthDiffers) {
    EXPECT_NE(crcOf("A"), crcOf("AA"));
    EXPECT_NE(crcOf(std::string(10, 'A')), crcOf(std::string(1000, 'A')));
}

// 用例 5：增量计算（以已有 CRC 为 seed）与一次性计算结果一致
TEST(Crc32Test, IncrementalMatchesOneShot) {
    const std::string part1 = "1234";
    const std::string part2 = "56789";
    const uint32_t first = crcOf(part1);
    const uint32_t combined =
        dataguard::crc32(reinterpret_cast<const uint8_t*>(part2.data()), part2.size(), first);
    EXPECT_EQ(combined, crcOf(part1 + part2));
}

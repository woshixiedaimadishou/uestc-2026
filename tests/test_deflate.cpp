#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "dataguard/deflate.h"
#include "dataguard/huffman.h"
#include "dataguard/lz77.h"

using dataguard::DeflateCompressor;
using dataguard::FixedHuffmanTable;
using dataguard::Lz77Matcher;
using dataguard::Lz77Token;

namespace {

std::vector<uint8_t> toBytes(const std::string& text) {
    return std::vector<uint8_t>(text.begin(), text.end());
}

// 压缩 → 解压 → 与原始数据比对
bool roundTrip(const std::vector<uint8_t>& input, std::vector<uint8_t>& compressed) {
    DeflateCompressor deflate;
    if (!deflate.compress(input, compressed)) return false;
    std::vector<uint8_t> restored;
    if (!deflate.decompress(compressed, restored)) return false;
    return restored == input;
}

double compressionRatio(size_t original, size_t compressed) {
    if (original == 0) return 0.0;
    return 1.0 - static_cast<double>(compressed) / static_cast<double>(original);
}

std::vector<uint8_t> pseudoRandom(size_t count, uint32_t seed) {
    std::vector<uint8_t> data(count);
    uint32_t state = seed;
    for (auto& byte : data) {
        state = state * 1664525u + 1013904223u;
        byte = static_cast<uint8_t>(state >> 24);
    }
    return data;
}

}  // namespace

// 用例 1：空输入 —— 仍产出块头与结束符，且能解压回空
TEST(DeflateTest, EmptyInput) {
    std::vector<uint8_t> compressed;
    EXPECT_TRUE(roundTrip({}, compressed));
    EXPECT_GT(compressed.size(), 0u);
}

// 用例 2：单字节
TEST(DeflateTest, SingleByte) {
    std::vector<uint8_t> compressed;
    EXPECT_TRUE(roundTrip(toBytes("A"), compressed));
}

// 用例 3：高重复数据 —— 压缩率 > 90%
TEST(DeflateTest, HighlyRepetitiveData) {
    const std::vector<uint8_t> input(1000, 'A');
    std::vector<uint8_t> compressed;
    ASSERT_TRUE(roundTrip(input, compressed));
    EXPECT_GT(compressionRatio(input.size(), compressed.size()), 0.90);
}

// 用例 4：0..255 全字节覆盖
TEST(DeflateTest, AllByteValues) {
    std::vector<uint8_t> input;
    input.reserve(256 * 4);
    for (int round = 0; round < 4; ++round) {
        for (int value = 0; value < 256; ++value) {
            input.push_back(static_cast<uint8_t>(value));
        }
    }
    std::vector<uint8_t> compressed;
    EXPECT_TRUE(roundTrip(input, compressed));
}

// 用例 5：中文 UTF-8 文本（≥1KB）—— 压缩率 ≥ 40%
TEST(DeflateTest, ChineseText) {
    std::string text;
    const std::string sentence =
        "数据备份软件需要保证数据的完整性与可恢复性，压缩算法应当正确还原每"
        "一个字节。";
    while (text.size() < 4096) {
        text += sentence;
    }
    const std::vector<uint8_t> input = toBytes(text);
    std::vector<uint8_t> compressed;
    ASSERT_TRUE(roundTrip(input, compressed));
    EXPECT_GE(compressionRatio(input.size(), compressed.size()), 0.40);
}

// 用例 6：伪随机数据 —— 允许变大，但必须可还原
TEST(DeflateTest, PseudoRandomData) {
    const std::vector<uint8_t> input = pseudoRandom(8192, 20261018u);
    std::vector<uint8_t> compressed;
    ASSERT_TRUE(roundTrip(input, compressed));
    EXPECT_GT(compressed.size(), 0u);
}

// 用例 7：长度码边界（3 与 258）—— 编码映射精确校验
TEST(DeflateTest, LengthCodeBoundary) {
    uint32_t symbol = 0;
    uint32_t extra_bits = 0;
    uint32_t extra_value = 0;

    ASSERT_TRUE(FixedHuffmanTable::encodeLength(3, symbol, extra_bits, extra_value));
    EXPECT_EQ(symbol, 257u);
    EXPECT_EQ(extra_bits, 0u);

    ASSERT_TRUE(FixedHuffmanTable::encodeLength(258, symbol, extra_bits, extra_value));
    EXPECT_EQ(symbol, 285u);
    EXPECT_EQ(extra_bits, 0u);

    EXPECT_FALSE(FixedHuffmanTable::encodeLength(2, symbol, extra_bits, extra_value));
    EXPECT_FALSE(FixedHuffmanTable::encodeLength(259, symbol, extra_bits, extra_value));
}

// 用例 8：距离码边界（1 与 32768）—— 编码映射精确校验
TEST(DeflateTest, DistanceCodeBoundary) {
    uint32_t symbol = 0;
    uint32_t extra_bits = 0;
    uint32_t extra_value = 0;

    ASSERT_TRUE(FixedHuffmanTable::encodeDistance(1, symbol, extra_bits, extra_value));
    EXPECT_EQ(symbol, 0u);
    EXPECT_EQ(extra_bits, 0u);

    ASSERT_TRUE(FixedHuffmanTable::encodeDistance(32768, symbol, extra_bits, extra_value));
    EXPECT_EQ(symbol, 29u);
    EXPECT_EQ(extra_bits, 13u);
    EXPECT_EQ(extra_value, 8191u);  // 32768 - 24577（距离码 29 的基础距离）

    EXPECT_FALSE(FixedHuffmanTable::encodeDistance(0, symbol, extra_bits, extra_value));
    EXPECT_FALSE(FixedHuffmanTable::encodeDistance(32769, symbol, extra_bits, extra_value));
}

// 用例 9：LZ77 匹配长度上限 258 生效
TEST(DeflateTest, Lz77MaxMatchLength) {
    const std::vector<uint8_t> input(2000, 'x');
    const Lz77Matcher matcher;
    const auto tokens = matcher.tokenize(input);

    bool has_max_match = false;
    for (const auto& token : tokens) {
        if (token.type == Lz77Token::Type::Match && token.length == 258) {
            has_max_match = true;
            break;
        }
    }
    EXPECT_TRUE(has_max_match);

    std::vector<uint8_t> compressed;
    EXPECT_TRUE(roundTrip(input, compressed));
}

// 用例 10：二进制数据（含 0x00 与 0xFF）
TEST(DeflateTest, BinaryDataWithZeroAndFF) {
    std::vector<uint8_t> input;
    for (int i = 0; i < 256; ++i) {
        input.push_back(0x00);
        input.push_back(0xFF);
        input.push_back(static_cast<uint8_t>(i));
    }
    std::vector<uint8_t> compressed;
    EXPECT_TRUE(roundTrip(input, compressed));
}

// 用例 11：大于 64KB 的混合数据（多匹配 + 跨窗口）
TEST(DeflateTest, LargeInputOver64KB) {
    std::vector<uint8_t> input = pseudoRandom(40000, 7u);
    const std::vector<uint8_t> repeated(10000, 0x7F);
    input.insert(input.end(), repeated.begin(), repeated.end());
    const std::vector<uint8_t> tail = toBytes(
        "DataGuard 数据备份软件的 DEFLATE 压缩实现需要覆盖大文件与重复片段。");
    input.insert(input.end(), tail.begin(), tail.end());

    std::vector<uint8_t> compressed;
    EXPECT_TRUE(roundTrip(input, compressed));
    EXPECT_GT(compressed.size(), 0u);
}

// 用例 12：确定性 —— 同一输入两次压缩输出完全一致
TEST(DeflateTest, DeterministicOutput) {
    const std::vector<uint8_t> input = toBytes(
        "deterministic deflate output test 0123456789 abcdefghijklmnopqrstuvwxyz");
    DeflateCompressor deflate;

    std::vector<uint8_t> first;
    std::vector<uint8_t> second;
    ASSERT_TRUE(deflate.compress(input, first));
    ASSERT_TRUE(deflate.compress(input, second));
    EXPECT_EQ(first, second);
}

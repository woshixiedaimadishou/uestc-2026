// 三种压缩方案的单元测试：往返一致性、边界数据、方案标识、相对效果
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "dataguard/compressor.h"
#include "dataguard/deflate.h"
#include "dataguard/huffman_only.h"
#include "dataguard/lz77_only.h"

namespace {

std::vector<uint8_t> toBytes(const std::string& text) {
    return std::vector<uint8_t>(text.begin(), text.end());
}

// 可重复的伪随机数据（不用 std::random 以保证跨平台一致）
std::vector<uint8_t> pseudoRandom(size_t size, uint32_t seed = 12345) {
    std::vector<uint8_t> out(size);
    uint32_t state = seed;
    for (size_t i = 0; i < size; ++i) {
        state = state * 1664525u + 1013904223u;
        out[i] = static_cast<uint8_t>(state >> 24);
    }
    return out;
}

std::vector<dataguard::Codec> allCodecs() {
    return {dataguard::Codec::Deflate, dataguard::Codec::HuffmanOnly, dataguard::Codec::Lz77Only};
}

}  // namespace

class CodecTest : public ::testing::TestWithParam<dataguard::Codec> {};

TEST_P(CodecTest, RoundTripPlainText) {
    const auto codec = GetParam();
    const auto compressor = dataguard::makeCompressor(codec);
    ASSERT_NE(compressor, nullptr);

    for (const std::string& text : {
             std::string("abcabcabcabcabcabcabcabc"),
             std::string("The quick brown fox jumps over the lazy dog. "),
             std::string("数据备份软件需要保证数据的完整性与可恢复性"),
         }) {
        const auto input = toBytes(text);
        std::vector<uint8_t> packed;
        std::vector<uint8_t> restored;
        ASSERT_TRUE(compressor->compress(input, packed)) << text;
        ASSERT_TRUE(compressor->decompress(packed, restored)) << text;
        EXPECT_EQ(restored, input) << text;
    }
}

TEST_P(CodecTest, RoundTripBinary) {
    const auto codec = GetParam();
    const auto compressor = dataguard::makeCompressor(codec);
    ASSERT_NE(compressor, nullptr);

    std::vector<uint8_t> bytes;
    for (int i = 0; i < 4; ++i) {
        for (int value = 0; value < 256; ++value) bytes.push_back(static_cast<uint8_t>(value));
    }

    std::vector<uint8_t> packed;
    std::vector<uint8_t> restored;
    ASSERT_TRUE(compressor->compress(bytes, packed));
    ASSERT_TRUE(compressor->decompress(packed, restored));
    EXPECT_EQ(restored, bytes);
}

TEST_P(CodecTest, RoundTripEmptyAndSingleByte) {
    const auto codec = GetParam();
    const auto compressor = dataguard::makeCompressor(codec);
    ASSERT_NE(compressor, nullptr);

    std::vector<uint8_t> empty;
    std::vector<uint8_t> packed;
    std::vector<uint8_t> restored;
    ASSERT_TRUE(compressor->compress(empty, packed));
    ASSERT_TRUE(compressor->decompress(packed, restored));
    EXPECT_TRUE(restored.empty());

    const std::vector<uint8_t> one{0x41};
    ASSERT_TRUE(compressor->compress(one, packed));
    ASSERT_TRUE(compressor->decompress(packed, restored));
    EXPECT_EQ(restored, one);
}

TEST_P(CodecTest, RoundTripLargeInput) {
    const auto codec = GetParam();
    const auto compressor = dataguard::makeCompressor(codec);
    ASSERT_NE(compressor, nullptr);

    // 200KB：文本重复 + 伪随机混合
    std::vector<uint8_t> input;
    for (int i = 0; i < 2000; ++i) {
        const std::string line = "line " + std::to_string(i % 50) + ": the quick brown fox\n";
        input.insert(input.end(), line.begin(), line.end());
    }
    const auto noise = pseudoRandom(20000);
    input.insert(input.end(), noise.begin(), noise.end());

    std::vector<uint8_t> packed;
    std::vector<uint8_t> restored;
    ASSERT_TRUE(compressor->compress(input, packed));
    ASSERT_TRUE(compressor->decompress(packed, restored));
    EXPECT_EQ(restored, input);
}

TEST_P(CodecTest, CorruptedInputIsRejected) {
    const auto codec = GetParam();
    const auto compressor = dataguard::makeCompressor(codec);
    ASSERT_NE(compressor, nullptr);

    std::vector<uint8_t> packed;
    std::vector<uint8_t> restored;
    ASSERT_TRUE(compressor->compress(toBytes("hello hello hello world"), packed));
    packed[0] = 0xFF;  // 破坏版本号
    EXPECT_FALSE(compressor->decompress(packed, restored));
}

INSTANTIATE_TEST_SUITE_P(AllCodecs, CodecTest, ::testing::ValuesIn(allCodecs()));

// ---------- 方案标识 ----------

TEST(CodecRegistryTest, IdsMatchArchiveFormat) {
    EXPECT_EQ(dataguard::codecId(dataguard::Codec::None), 0);
    EXPECT_EQ(dataguard::codecId(dataguard::Codec::Deflate), 1);
    EXPECT_EQ(dataguard::codecId(dataguard::Codec::HuffmanOnly), 2);
    EXPECT_EQ(dataguard::codecId(dataguard::Codec::Lz77Only), 3);

    EXPECT_EQ(dataguard::codecFromId(0), dataguard::Codec::None);
    EXPECT_EQ(dataguard::codecFromId(1), dataguard::Codec::Deflate);
    EXPECT_EQ(dataguard::codecFromId(2), dataguard::Codec::HuffmanOnly);
    EXPECT_EQ(dataguard::codecFromId(3), dataguard::Codec::Lz77Only);
}

TEST(CodecRegistryTest, NamesAreParsed) {
    EXPECT_EQ(dataguard::codecFromName("deflate"), dataguard::Codec::Deflate);
    EXPECT_EQ(dataguard::codecFromName("Huffman"), dataguard::Codec::HuffmanOnly);
    EXPECT_EQ(dataguard::codecFromName("huffman-only"), dataguard::Codec::HuffmanOnly);
    EXPECT_EQ(dataguard::codecFromName("lz77"), dataguard::Codec::Lz77Only);
    EXPECT_EQ(dataguard::codecFromName("LZ"), dataguard::Codec::Lz77Only);
    EXPECT_EQ(dataguard::codecFromName("none"), dataguard::Codec::None);
    EXPECT_STREQ(dataguard::codecName(dataguard::Codec::HuffmanOnly), "huffman");
}

// ---------- 相对效果：不同数据用不同方案 ----------

TEST(CodecEffectivenessTest, RepetitiveDataFavorsLz77) {
    const auto input = toBytes(std::string(4000, 'a'));
    std::vector<uint8_t> lz;
    std::vector<uint8_t> huff;
    dataguard::Lz77OnlyCompressor().compress(input, lz);
    dataguard::HuffmanOnlyCompressor().compress(input, huff);

    // 全是同一个字符：LZ77 一个匹配就够；Huffman 至少要 1 位/字节 + 码表
    EXPECT_LT(lz.size(), huff.size());
    EXPECT_LT(lz.size(), input.size());
}

TEST(CodecEffectivenessTest, SkewedFrequenciesFavorHuffmanButNotRandom) {
    // 90% 是 'a'，10% 是少量其他字符 → Huffman 明显变小
    std::string skewed;
    for (int i = 0; i < 9000; ++i) skewed.push_back('a');
    for (int i = 0; i < 1000; ++i) skewed.push_back(static_cast<char>('b' + (i % 4)));

    std::vector<uint8_t> packed;
    dataguard::HuffmanOnlyCompressor().compress(toBytes(skewed), packed);
    EXPECT_LT(packed.size(), skewed.size());

    // 均匀随机数据：Huffman 无法压缩（允许略大，因为要存码表）
    const auto random = pseudoRandom(20000);
    std::vector<uint8_t> packed_random;
    dataguard::HuffmanOnlyCompressor().compress(random, packed_random);
    EXPECT_GT(packed_random.size(), random.size() * 9 / 10);  // 不会“神奇地”变小很多
}

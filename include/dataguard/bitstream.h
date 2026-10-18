#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace dataguard {

// 位流写入器（DEFLATE 位序规则）：
//   - writeBits     ：LSB-first，用于块头（BFINAL/BTYPE）与长度/距离的 extra bits
//   - writeHuffman  ：MSB-first，用于 Huffman 码字（DEFLATE 规定码字高位先写）
class BitWriter {
public:
    void writeBits(uint32_t value, int nbits);
    void writeHuffman(uint32_t code, int nbits);
    void alignToByte();
    std::vector<uint8_t> finish();
    size_t bitPosition() const { return buffer_.size() * 8 + static_cast<size_t>(bit_count_); }

private:
    void pushBit(uint32_t bit);

    std::vector<uint8_t> buffer_;
    uint32_t bit_buffer_ = 0;
    int bit_count_ = 0;
};

// 位流读取器：与 BitWriter 对称
class BitReader {
public:
    explicit BitReader(const std::vector<uint8_t>& data) : data_(data) {}

    bool readBits(int nbits, uint32_t& value);      // LSB-first
    bool readHuffman(int nbits, uint32_t& code);    // MSB-first
    bool alignToByte();
    bool eof() const { return byte_pos_ >= data_.size() && bit_count_ == 0; }
    size_t bitPosition() const;

private:
    bool ensureBits();

    const std::vector<uint8_t>& data_;
    size_t byte_pos_ = 0;
    uint32_t bit_buffer_ = 0;
    int bit_count_ = 0;
    size_t consumed_bits_ = 0;
};

}  // namespace dataguard

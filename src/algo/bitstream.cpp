#include "dataguard/bitstream.h"

namespace dataguard {

void BitWriter::pushBit(uint32_t bit) {
    bit_buffer_ |= (bit & 1u) << bit_count_;
    ++bit_count_;
    if (bit_count_ == 8) {
        buffer_.push_back(static_cast<uint8_t>(bit_buffer_ & 0xFFu));
        bit_buffer_ = 0;
        bit_count_ = 0;
    }
}

void BitWriter::writeBits(uint32_t value, int nbits) {
    for (int i = 0; i < nbits; ++i) {
        pushBit((value >> i) & 1u);  // LSB-first
    }
}

void BitWriter::writeHuffman(uint32_t code, int nbits) {
    for (int i = nbits - 1; i >= 0; --i) {
        pushBit((code >> i) & 1u);  // MSB-first
    }
}

void BitWriter::alignToByte() {
    while (bit_count_ != 0) {
        pushBit(0u);
    }
}

std::vector<uint8_t> BitWriter::finish() {
    if (bit_count_ != 0) {
        buffer_.push_back(static_cast<uint8_t>(bit_buffer_ & 0xFFu));
        bit_buffer_ = 0;
        bit_count_ = 0;
    }
    return buffer_;
}

bool BitReader::ensureBits() {
    if (bit_count_ > 0) return true;
    if (byte_pos_ >= data_.size()) return false;
    bit_buffer_ = data_[byte_pos_++];
    bit_count_ = 8;
    return true;
}

bool BitReader::readBits(int nbits, uint32_t& value) {
    value = 0;
    for (int i = 0; i < nbits; ++i) {
        if (!ensureBits()) return false;
        value |= (bit_buffer_ & 1u) << i;  // LSB-first
        bit_buffer_ >>= 1;
        --bit_count_;
        ++consumed_bits_;
    }
    return true;
}

bool BitReader::readHuffman(int nbits, uint32_t& code) {
    code = 0;
    for (int i = 0; i < nbits; ++i) {
        if (!ensureBits()) return false;
        code = (code << 1) | (bit_buffer_ & 1u);  // MSB-first
        bit_buffer_ >>= 1;
        --bit_count_;
        ++consumed_bits_;
    }
    return true;
}

bool BitReader::alignToByte() {
    while (bit_count_ != 0) {
        bit_buffer_ >>= 1;
        --bit_count_;
        ++consumed_bits_;
    }
    return true;
}

size_t BitReader::bitPosition() const {
    return consumed_bits_;
}

}  // namespace dataguard

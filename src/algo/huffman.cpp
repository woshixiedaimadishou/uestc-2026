#include "dataguard/huffman.h"

#include <cstdint>

namespace dataguard {
namespace {

// 长度码表（RFC 1951 §3.2.5）：符号 257..285 → 基础长度 + extra 位数
const uint32_t kLengthBase[29] = {3,   4,   5,   6,   7,   8,   9,   10,  11,  13,
                                  15,  17,  19,  23,  27,  31,  35,  43,  51,  59,
                                  67,  83,  99,  115, 131, 163, 195, 227, 258};
const uint8_t kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                  2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

// 距离码表（RFC 1951 §3.2.5）：符号 0..29 → 基础距离 + extra 位数
const uint32_t kDistanceBase[30] = {1,     2,     3,     4,     5,     7,     9,     13,
                                    17,    25,    33,    49,    65,    97,    129,   193,
                                    257,   385,   513,   769,   1025,  1537,  2049,  3073,
                                    4097,  6145,  8193,  12289, 16385, 24577};
const uint8_t kDistanceExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3,  3,  4,  4,  5,  5,  6,
                                    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

}  // namespace

HuffmanEntry FixedHuffmanTable::literalLength(uint32_t symbol) {
    HuffmanEntry entry{0, 0};
    if (symbol <= 143) {
        entry.code = static_cast<uint16_t>(0x30 + symbol);
        entry.bits = 8;
    } else if (symbol <= 255) {
        entry.code = static_cast<uint16_t>(0x190 + (symbol - 144));
        entry.bits = 9;
    } else if (symbol <= 279) {
        entry.code = static_cast<uint16_t>(0x00 + (symbol - 256));
        entry.bits = 7;
    } else {
        entry.code = static_cast<uint16_t>(0xC0 + (symbol - 280));
        entry.bits = 8;
    }
    return entry;
}

HuffmanEntry FixedHuffmanTable::distance(uint32_t distance_code) {
    return HuffmanEntry{static_cast<uint16_t>(distance_code & 0x1F), 5};
}

bool FixedHuffmanTable::decodeLiteralLength(BitReader& reader, uint32_t& symbol) {
    uint32_t code = 0;
    if (!reader.readHuffman(7, code)) return false;
    if (code <= 0x17) {  // 7 位码：符号 256..279
        symbol = 256 + code;
        return true;
    }

    uint32_t bit = 0;
    if (!reader.readHuffman(1, bit)) return false;
    code = (code << 1) | bit;
    if (code >= 0x30 && code <= 0xBF) {  // 8 位码：符号 0..143
        symbol = code - 0x30;
        return true;
    }
    if (code >= 0xC0 && code <= 0xC7) {  // 8 位码：符号 280..287
        symbol = 280 + (code - 0xC0);
        return true;
    }

    if (!reader.readHuffman(1, bit)) return false;
    code = (code << 1) | bit;
    if (code >= 0x190 && code <= 0x1FF) {  // 9 位码：符号 144..255
        symbol = 144 + (code - 0x190);
        return true;
    }
    return false;
}

bool FixedHuffmanTable::decodeDistanceCode(BitReader& reader, uint32_t& distance_code) {
    return reader.readHuffman(5, distance_code);
}

bool FixedHuffmanTable::encodeLength(uint32_t length,
                                     uint32_t& symbol,
                                     uint32_t& extra_bits,
                                     uint32_t& extra_value) {
    if (length < 3 || length > 258) return false;

    // RFC 1951：长度 258 使用专用码 285（无 extra bits），
    // 否则会错误落入码 284 的区间（227 起、5 位 extra）。
    if (length == 258) {
        symbol = 285;
        extra_bits = 0;
        extra_value = 0;
        return true;
    }

    for (uint32_t i = 0; i < 28; ++i) {
        const uint32_t base = kLengthBase[i];
        const uint32_t bits = kLengthExtra[i];
        const uint32_t span = (bits == 0) ? 1u : (1u << bits);
        if (length >= base && length < base + span) {
            symbol = 257 + i;
            extra_bits = bits;
            extra_value = length - base;
            return true;
        }
    }
    return false;
}

bool FixedHuffmanTable::encodeDistance(uint32_t distance,
                                       uint32_t& symbol,
                                       uint32_t& extra_bits,
                                       uint32_t& extra_value) {
    if (distance < 1 || distance > 32768) return false;
    for (uint32_t i = 0; i < 30; ++i) {
        const uint32_t base = kDistanceBase[i];
        const uint32_t bits = kDistanceExtra[i];
        const uint32_t span = (bits == 0) ? 1u : (1u << bits);
        if (distance >= base && distance < base + span) {
            symbol = i;
            extra_bits = bits;
            extra_value = distance - base;
            return true;
        }
    }
    return false;
}

bool FixedHuffmanTable::decodeLengthValue(BitReader& reader,
                                          uint32_t symbol,
                                          uint32_t& length) {
    if (symbol < 257 || symbol > 285) return false;
    const uint32_t index = symbol - 257;
    uint32_t extra = 0;
    const uint32_t bits = kLengthExtra[index];
    if (bits > 0 && !reader.readBits(bits, extra)) return false;
    length = kLengthBase[index] + extra;
    return true;
}

bool FixedHuffmanTable::decodeDistanceValue(BitReader& reader,
                                            uint32_t symbol,
                                            uint32_t& distance) {
    if (symbol > 29) return false;
    uint32_t extra = 0;
    const uint32_t bits = kDistanceExtra[symbol];
    if (bits > 0 && !reader.readBits(bits, extra)) return false;
    distance = kDistanceBase[symbol] + extra;
    return true;
}

}  // namespace dataguard

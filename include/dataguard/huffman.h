#pragma once

#include <cstdint>

#include "dataguard/bitstream.h"

namespace dataguard {

struct HuffmanEntry {
    uint16_t code;
    uint8_t bits;
};

// RFC 1951 固定 Huffman 码表（BTYPE=01）：
//   literal/length：0-143 → 8 位 0x30+; 144-255 → 9 位 0x190+;
//                   256-279 → 7 位 0x00+; 280-287 → 8 位 0xC0+
//   distance：5 位，码值即 distance 码
class FixedHuffmanTable {
public:
    static HuffmanEntry literalLength(uint32_t symbol);
    static HuffmanEntry distance(uint32_t distance_code);

    // 解码：从位流读出一个 literal/length 符号（0..287）
    static bool decodeLiteralLength(BitReader& reader, uint32_t& symbol);
    // 解码：从位流读出一个 distance 码（0..29）
    static bool decodeDistanceCode(BitReader& reader, uint32_t& distance_code);

    // 编码辅助：length(3..258) → 符号(257..285) + extra 位数 + extra 值
    static bool encodeLength(uint32_t length,
                             uint32_t& symbol,
                             uint32_t& extra_bits,
                             uint32_t& extra_value);
    // 编码辅助：distance(1..32768) → distance 码(0..29) + extra 位数 + extra 值
    static bool encodeDistance(uint32_t distance,
                               uint32_t& symbol,
                               uint32_t& extra_bits,
                               uint32_t& extra_value);

    // 解码辅助：length 符号 + extra → 实际长度
    static bool decodeLengthValue(BitReader& reader, uint32_t symbol, uint32_t& length);
    // 解码辅助：distance 码 + extra → 实际距离
    static bool decodeDistanceValue(BitReader& reader, uint32_t symbol, uint32_t& distance);
};

}  // namespace dataguard

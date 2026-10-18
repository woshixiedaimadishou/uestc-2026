#include "dataguard/deflate.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "dataguard/bitstream.h"
#include "dataguard/huffman.h"
#include "dataguard/lz77.h"

namespace dataguard {

bool DeflateCompressor::compress(const std::vector<uint8_t>& in,
                                 std::vector<uint8_t>& out) const {
    const Lz77Matcher matcher;
    const std::vector<Lz77Token> tokens = matcher.tokenize(in);

    BitWriter writer;
    writer.writeBits(1, 1);  // BFINAL = 1
    writer.writeBits(1, 2);  // BTYPE = 01（固定 Huffman）

    for (const auto& token : tokens) {
        if (token.type == Lz77Token::Type::Literal) {
            const HuffmanEntry entry = FixedHuffmanTable::literalLength(token.literal);
            writer.writeHuffman(entry.code, entry.bits);
            continue;
        }

        uint32_t length_symbol = 0;
        uint32_t length_extra_bits = 0;
        uint32_t length_extra_value = 0;
        if (!FixedHuffmanTable::encodeLength(token.length, length_symbol, length_extra_bits,
                                             length_extra_value)) {
            return false;
        }
        const HuffmanEntry length_entry = FixedHuffmanTable::literalLength(length_symbol);
        writer.writeHuffman(length_entry.code, length_entry.bits);
        if (length_extra_bits > 0) {
            writer.writeBits(length_extra_value, static_cast<int>(length_extra_bits));
        }

        uint32_t distance_symbol = 0;
        uint32_t distance_extra_bits = 0;
        uint32_t distance_extra_value = 0;
        if (!FixedHuffmanTable::encodeDistance(token.distance, distance_symbol,
                                               distance_extra_bits, distance_extra_value)) {
            return false;
        }
        const HuffmanEntry distance_entry = FixedHuffmanTable::distance(distance_symbol);
        writer.writeHuffman(distance_entry.code, distance_entry.bits);
        if (distance_extra_bits > 0) {
            writer.writeBits(distance_extra_value, static_cast<int>(distance_extra_bits));
        }
    }

    // 块结束符 256
    const HuffmanEntry end_of_block = FixedHuffmanTable::literalLength(256);
    writer.writeHuffman(end_of_block.code, end_of_block.bits);

    out = writer.finish();
    return true;
}

bool DeflateCompressor::decompress(const std::vector<uint8_t>& in,
                                   std::vector<uint8_t>& out) const {
    out.clear();
    BitReader reader(in);

    bool final_block = false;
    while (!final_block) {
        uint32_t bfinal = 0;
        uint32_t btype = 0;
        if (!reader.readBits(1, bfinal)) return false;
        if (!reader.readBits(2, btype)) return false;
        final_block = (bfinal == 1);

        if (btype != 1) {
            // 仅支持固定 Huffman（BTYPE=01）
            return false;
        }

        while (true) {
            uint32_t symbol = 0;
            if (!FixedHuffmanTable::decodeLiteralLength(reader, symbol)) return false;

            if (symbol < 256) {
                out.push_back(static_cast<uint8_t>(symbol));
                continue;
            }
            if (symbol == 256) break;  // 块结束

            uint32_t length = 0;
            uint32_t distance_code = 0;
            uint32_t distance = 0;
            if (!FixedHuffmanTable::decodeLengthValue(reader, symbol, length)) return false;
            if (!FixedHuffmanTable::decodeDistanceCode(reader, distance_code)) return false;
            if (!FixedHuffmanTable::decodeDistanceValue(reader, distance_code, distance)) {
                return false;
            }
            if (distance == 0 || distance > out.size()) return false;

            const size_t start = out.size() - distance;
            for (uint32_t i = 0; i < length; ++i) {
                // 允许重叠复制：逐字节取值，数据在 out 中动态增长
                const uint8_t byte = out[start + i];
                out.push_back(byte);
            }
        }
    }

    return true;
}

}  // namespace dataguard

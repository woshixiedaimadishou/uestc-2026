#include "dataguard/lz77_only.h"

#include <cstdint>
#include <vector>

#include "dataguard/bitstream.h"
#include "dataguard/lz77.h"

namespace dataguard {
namespace {

constexpr uint8_t kVersion = 1;

void appendU32(std::vector<uint8_t>& out, uint32_t value) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
}

uint32_t readU32(const std::vector<uint8_t>& in, size_t offset) {
    return static_cast<uint32_t>(in[offset]) |
           (static_cast<uint32_t>(in[offset + 1]) << 8) |
           (static_cast<uint32_t>(in[offset + 2]) << 16) |
           (static_cast<uint32_t>(in[offset + 3]) << 24);
}

}  // namespace

bool Lz77OnlyCompressor::compress(const std::vector<uint8_t>& in,
                                  std::vector<uint8_t>& out) const {
    out.clear();
    out.push_back(kVersion);
    appendU32(out, static_cast<uint32_t>(in.size()));
    if (in.empty()) return true;

    Lz77Matcher matcher;
    const std::vector<Lz77Token> tokens = matcher.tokenize(in);

    BitWriter writer;
    for (const auto& token : tokens) {
        if (token.type == Lz77Token::Type::Literal) {
            writer.writeBits(0, 1);
            writer.writeBits(token.literal, 8);
            continue;
        }
        writer.writeBits(1, 1);
        writer.writeBits(static_cast<uint32_t>(token.length) - 3u, 8);      // 3..258 → 0..255
        writer.writeBits(static_cast<uint32_t>(token.distance) - 1u, 15);   // 1..32768 → 0..32767
    }
    const std::vector<uint8_t> bits = writer.finish();
    out.insert(out.end(), bits.begin(), bits.end());
    return true;
}

bool Lz77OnlyCompressor::decompress(const std::vector<uint8_t>& in,
                                    std::vector<uint8_t>& out) const {
    out.clear();
    if (in.size() < 5 || in[0] != kVersion) return false;

    const uint32_t size = readU32(in, 1);
    if (size == 0) return true;

    const std::vector<uint8_t> payload(in.begin() + 5, in.end());
    BitReader reader(payload);
    out.reserve(size);

    while (out.size() < size) {
        uint32_t tag = 0;
        if (!reader.readBits(1, tag)) return false;

        if (tag == 0) {
            uint32_t byte = 0;
            if (!reader.readBits(8, byte)) return false;
            out.push_back(static_cast<uint8_t>(byte));
            continue;
        }

        uint32_t length_minus3 = 0;
        uint32_t distance_minus1 = 0;
        if (!reader.readBits(8, length_minus3)) return false;
        if (!reader.readBits(15, distance_minus1)) return false;

        const size_t length = static_cast<size_t>(length_minus3) + 3;
        const size_t distance = static_cast<size_t>(distance_minus1) + 1;
        if (distance > out.size()) return false;  // 距离越界，数据损坏
        const size_t start = out.size() - distance;
        for (size_t i = 0; i < length; ++i) {
            out.push_back(out[start + i]);  // 允许重叠复制
        }
    }
    return true;
}

}  // namespace dataguard

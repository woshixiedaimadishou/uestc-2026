#pragma once

#include "dataguard/compressor.h"

namespace dataguard {

// 只做 LZ77（重复替换），不做 Huffman 缩码。
// token 按位打包：1 位标志 + 字面 8 位 / 匹配 8 位长度 + 15 位距离。
//
// 数据格式（小端）：
//   [0]      version = 1
//   [1..5)   original_size (u32)
//   [5..)    位流：每个 token 先写 1 位标志（0=字面，1=匹配）
//              字面：8 位字节值
//              匹配：8 位 (length-3)，15 位 (distance-1)
class Lz77OnlyCompressor : public Compressor {
public:
    uint8_t id() const override { return kCompressorLz77; }
    bool compress(const std::vector<uint8_t>& in,
                  std::vector<uint8_t>& out) const override;
    bool decompress(const std::vector<uint8_t>& in,
                    std::vector<uint8_t>& out) const override;
    const char* name() const override { return "lz77"; }
};

}  // namespace dataguard

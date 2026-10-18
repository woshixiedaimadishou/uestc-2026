#pragma once

#include "dataguard/compressor.h"

namespace dataguard {

// DEFLATE 压缩（RFC 1951 子集）：
//   - LZ77 分词（Lz77Matcher）
//   - 固定 Huffman 编码（FixedHuffmanTable，BTYPE=01）
//   - 单块输出（BFINAL=1）
// 压缩/解压的位序规则见 bitstream.h。
class DeflateCompressor : public Compressor {
public:
    uint8_t id() const override { return 1; }
    bool compress(const std::vector<uint8_t>& in,
                  std::vector<uint8_t>& out) const override;
    bool decompress(const std::vector<uint8_t>& in,
                    std::vector<uint8_t>& out) const override;
    const char* name() const override { return "deflate"; }
};

}  // namespace dataguard

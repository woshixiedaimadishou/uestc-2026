#pragma once

#include "dataguard/compressor.h"

namespace dataguard {

// 只做 Huffman 编码（不做 LZ77 找重复）。
// 码表按本块数据的实际字节频率动态生成，随数据一起存（RLE 压缩），
// 因此对“字节分布不均匀”的文本有效，对随机/已压缩数据基本无收益。
//
// 数据格式（小端）：
//   [0]      version = 1
//   [1..5)   original_size (u32)
//   [5..7)   table_bytes (u16)   —— RLE 码长表的字节数
//   [7..)    RLE 码长表：(code_len u8, run u8) 若干对，覆盖 256 个符号
//   之后     位流：每个字节按其码长 MSB-first 写出
class HuffmanOnlyCompressor : public Compressor {
public:
    uint8_t id() const override { return kCompressorHuffman; }
    bool compress(const std::vector<uint8_t>& in,
                  std::vector<uint8_t>& out) const override;
    bool decompress(const std::vector<uint8_t>& in,
                    std::vector<uint8_t>& out) const override;
    const char* name() const override { return "huffman"; }
};

}  // namespace dataguard

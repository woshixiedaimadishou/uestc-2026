#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dataguard {

// 压缩方案编号（写进档案头第 8 字节）
//   0 = 不压缩
//   1 = DEFLATE（LZ77 找重复 + 固定 Huffman 缩码）
//   2 = 仅 Huffman（动态码表，只做频率压缩）
//   3 = 仅 LZ77（只做重复替换，不缩码）
enum class Codec : uint8_t {
    None = 0,
    Deflate = 1,
    HuffmanOnly = 2,
    Lz77Only = 3,
};

constexpr uint8_t kCompressorNone = 0;
constexpr uint8_t kCompressorDeflate = 1;
constexpr uint8_t kCompressorHuffman = 2;
constexpr uint8_t kCompressorLz77 = 3;

// 字符串 ↔ 枚举（CLI 与配置文件使用）
Codec codecFromName(const std::string& name);
const char* codecName(Codec codec);
uint8_t codecId(Codec codec);
Codec codecFromId(uint8_t id);

// 压缩器抽象接口：档案层只依赖该接口，压缩算法实现可替换
class Compressor {
public:
    virtual ~Compressor() = default;
    virtual uint8_t id() const = 0;
    virtual bool compress(const std::vector<uint8_t>& in,
                          std::vector<uint8_t>& out) const = 0;
    virtual bool decompress(const std::vector<uint8_t>& in,
                            std::vector<uint8_t>& out) const = 0;
    virtual const char* name() const = 0;
};

// 不压缩（恒等变换）
class NoCompressor : public Compressor {
public:
    uint8_t id() const override { return kCompressorNone; }
    bool compress(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) const override {
        out = in;
        return true;
    }
    bool decompress(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) const override {
        out = in;
        return true;
    }
    const char* name() const override { return "none"; }
};

// 工厂：按方案创建压缩器（未知方案返回 nullptr）
std::unique_ptr<Compressor> makeCompressor(Codec codec);

}  // namespace dataguard

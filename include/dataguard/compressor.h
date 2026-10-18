#pragma once

#include <cstdint>
#include <vector>

namespace dataguard {

// 压缩器抽象接口：档案层只依赖该接口，压缩算法实现可替换
class Compressor {
public:
    virtual ~Compressor() = default;
    virtual uint8_t id() const = 0;  // 0=none, 1=deflate
    virtual bool compress(const std::vector<uint8_t>& in,
                          std::vector<uint8_t>& out) const = 0;
    virtual bool decompress(const std::vector<uint8_t>& in,
                            std::vector<uint8_t>& out) const = 0;
    virtual const char* name() const = 0;
};

// 不压缩（恒等变换）
class NoCompressor : public Compressor {
public:
    uint8_t id() const override { return 0; }
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

}  // namespace dataguard

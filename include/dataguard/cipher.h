#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dataguard {

// 加解密抽象接口：档案层只依赖该接口
class Cipher {
public:
    virtual ~Cipher() = default;
    virtual uint8_t id() const = 0;  // 0=none, 1=stream
    virtual bool encrypt(const std::vector<uint8_t>& in,
                         std::vector<uint8_t>& out) const = 0;
    virtual bool decrypt(const std::vector<uint8_t>& in,
                         std::vector<uint8_t>& out) const = 0;
    virtual const char* name() const = 0;
};

// 不加密（恒等变换）
class NoCipher : public Cipher {
public:
    uint8_t id() const override { return 0; }
    bool encrypt(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) const override {
        out = in;
        return true;
    }
    bool decrypt(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) const override {
        out = in;
        return true;
    }
    const char* name() const override { return "none"; }
};

// 自研流密码（类 RC4）：
//   key = password(UTF-8) ‖ iv(16 字节)
//   KSA 256 轮 → 丢弃 PRGA 前 256 字节 → 逐字节 XOR
// 加密与解密是同一操作。
class StreamCipher : public Cipher {
public:
    explicit StreamCipher(const std::string& password) : password_(password) {}

    uint8_t id() const override { return 1; }
    bool encrypt(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) const override;
    bool decrypt(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) const override;
    const char* name() const override { return "stream"; }

    // 返回当前 IV（首次访问时生成 16 字节随机 IV）
    const std::vector<uint8_t>& iv() const;
    // 解密时注入档案头中的 IV
    void set_iv(const std::vector<uint8_t>& iv) { iv_ = iv; }

    // 口令校验值：用当前 key 流加密固定串 "DG-PASS-CHECK" 的前 4 字节
    std::array<uint8_t, 4> password_check() const;

private:
    void generateIv() const;
    void crypt(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) const;

    std::string password_;
    mutable std::vector<uint8_t> iv_;
};

// 流式加解密：构造时完成 KSA，之后可分多块连续处理，
// 密钥流位置在块之间保持（用于大档案边读边写，避免整包拷贝）。
class StreamCipherStream {
public:
    StreamCipherStream(const std::string& password, const std::vector<uint8_t>& iv);

    // 处理一块数据，结果追加/覆盖写入 out（out 会被 resize 为 size）
    void process(const uint8_t* data, size_t size, std::vector<uint8_t>& out);

private:
    uint8_t s_[256] = {};
    int i_ = 0;
    int j_ = 0;
};


}  // namespace dataguard

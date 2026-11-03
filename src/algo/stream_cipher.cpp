#include "dataguard/cipher.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace dataguard {
namespace {

// 口令校验用固定串
const char kPasswordCheckText[] = "DG-PASS-CHECK";

// 核心流密码：KSA + 丢弃前 256 字节 + PRGA 逐字节 XOR
void streamXor(const std::string& password,
               const std::vector<uint8_t>& iv,
               const std::vector<uint8_t>& in,
               std::vector<uint8_t>& out) {
    StreamCipherStream stream(password, iv);
    stream.process(in.data(), in.size(), out);
}

}  // namespace

StreamCipherStream::StreamCipherStream(const std::string& password,
                                       const std::vector<uint8_t>& iv) {
    std::vector<uint8_t> key(password.begin(), password.end());
    key.insert(key.end(), iv.begin(), iv.end());
    if (key.empty()) key.push_back(0);

    for (int i = 0; i < 256; ++i) s_[i] = static_cast<uint8_t>(i);

    int j = 0;
    for (int i = 0; i < 256; ++i) {
        j = (j + s_[i] + key[static_cast<size_t>(i) % key.size()]) & 0xFF;
        std::swap(s_[i], s_[j]);
    }

    // 丢弃前 256 字节输出（RC4 惯例，规避弱密钥）。
    // 注意：j_ 必须保留到这里的状态，否则密钥流与旧档案不一致。
    for (int k = 0; k < 256; ++k) {
        i_ = (i_ + 1) & 0xFF;
        j_ = (j_ + s_[i_]) & 0xFF;
        std::swap(s_[i_], s_[j_]);
    }
}

void StreamCipherStream::process(const uint8_t* data, size_t size,
                                 std::vector<uint8_t>& out) {
    out.resize(size);
    for (size_t k = 0; k < size; ++k) {
        i_ = (i_ + 1) & 0xFF;
        j_ = (j_ + s_[i_]) & 0xFF;
        std::swap(s_[i_], s_[j_]);
        const uint8_t keystream = s_[(s_[i_] + s_[j_]) & 0xFF];
        out[k] = static_cast<uint8_t>(data[k] ^ keystream);
    }
}

void StreamCipher::generateIv() const {
    if (iv_.size() == 16) return;
    iv_.assign(16, 0);

    std::random_device device;
    const uint64_t time_seed = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    uint64_t state = (static_cast<uint64_t>(device()) << 32) ^
                     static_cast<uint64_t>(device()) ^ time_seed;

    for (auto& byte : iv_) {
        // xorshift64* 混合，保证 IV 分散
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        state *= 2685821657736338717ULL;
        byte = static_cast<uint8_t>(state >> 32);
    }
}

const std::vector<uint8_t>& StreamCipher::iv() const {
    generateIv();
    return iv_;
}

void StreamCipher::crypt(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) const {
    generateIv();
    streamXor(password_, iv_, in, out);
}

bool StreamCipher::encrypt(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) const {
    crypt(in, out);
    return true;
}

bool StreamCipher::decrypt(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) const {
    crypt(in, out);
    return true;
}

std::array<uint8_t, 4> StreamCipher::password_check() const {
    std::vector<uint8_t> plain(kPasswordCheckText,
                               kPasswordCheckText + sizeof(kPasswordCheckText) - 1);
    std::vector<uint8_t> encrypted;
    crypt(plain, encrypted);

    std::array<uint8_t, 4> result{};
    for (size_t i = 0; i < 4 && i < encrypted.size(); ++i) {
        result[i] = encrypted[i];
    }
    return result;
}

}  // namespace dataguard

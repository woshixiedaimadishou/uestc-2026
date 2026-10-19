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
    std::vector<uint8_t> key(password.begin(), password.end());
    key.insert(key.end(), iv.begin(), iv.end());
    if (key.empty()) key.push_back(0);

    uint8_t s[256];
    for (int i = 0; i < 256; ++i) s[i] = static_cast<uint8_t>(i);

    int j = 0;
    for (int i = 0; i < 256; ++i) {
        j = (j + s[i] + key[static_cast<size_t>(i) % key.size()]) & 0xFF;
        std::swap(s[i], s[j]);
    }

    int i_state = 0;
    j = 0;
    // 丢弃前 256 字节输出（RC4 惯例，规避弱密钥）
    for (int k = 0; k < 256; ++k) {
        i_state = (i_state + 1) & 0xFF;
        j = (j + s[i_state]) & 0xFF;
        std::swap(s[i_state], s[j]);
    }

    out.resize(in.size());
    for (size_t k = 0; k < in.size(); ++k) {
        i_state = (i_state + 1) & 0xFF;
        j = (j + s[i_state]) & 0xFF;
        std::swap(s[i_state], s[j]);
        const uint8_t keystream = s[(s[i_state] + s[j]) & 0xFF];
        out[k] = static_cast<uint8_t>(in[k] ^ keystream);
    }
}

}  // namespace

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

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "dataguard/cipher.h"

using dataguard::NoCipher;
using dataguard::StreamCipher;

namespace {

std::vector<uint8_t> toBytes(const std::string& text) {
    return std::vector<uint8_t>(text.begin(), text.end());
}

// 用指定密码与 IV 加密
std::vector<uint8_t> encryptWith(const std::string& password,
                                 const std::vector<uint8_t>& iv,
                                 const std::vector<uint8_t>& plain) {
    StreamCipher cipher(password);
    cipher.set_iv(iv);
    std::vector<uint8_t> out;
    cipher.encrypt(plain, out);
    return out;
}

// 用指定密码与 IV 解密
std::vector<uint8_t> decryptWith(const std::string& password,
                                 const std::vector<uint8_t>& iv,
                                 const std::vector<uint8_t>& cipher_text) {
    StreamCipher cipher(password);
    cipher.set_iv(iv);
    std::vector<uint8_t> out;
    cipher.decrypt(cipher_text, out);
    return out;
}

}  // namespace

// 用例 1：加解密回环（同一 IV）
TEST(CipherTest, RoundTrip) {
    const std::vector<uint8_t> plain = toBytes("DataGuard 数据备份软件 encryption test");
    StreamCipher cipher("passw0rd");
    const std::vector<uint8_t> iv = cipher.iv();

    std::vector<uint8_t> cipher_text;
    ASSERT_TRUE(cipher.encrypt(plain, cipher_text));
    EXPECT_NE(cipher_text, plain);

    const std::vector<uint8_t> restored = decryptWith("passw0rd", iv, cipher_text);
    EXPECT_EQ(restored, plain);
}

// 用例 2：IV 长度固定 16 字节
TEST(CipherTest, IvLengthIsSixteen) {
    StreamCipher cipher("pwd");
    EXPECT_EQ(cipher.iv().size(), 16u);
}

// 用例 3：不同 IV 加密同一明文，密文不同
TEST(CipherTest, DifferentIvProducesDifferentCipher) {
    const std::vector<uint8_t> plain = toBytes("same plaintext");
    const std::vector<uint8_t> iv1(16, 0x11);
    const std::vector<uint8_t> iv2(16, 0x22);

    const auto c1 = encryptWith("key", iv1, plain);
    const auto c2 = encryptWith("key", iv2, plain);
    EXPECT_NE(c1, c2);
    EXPECT_EQ(decryptWith("key", iv1, c1), plain);
    EXPECT_EQ(decryptWith("key", iv2, c2), plain);
}

// 用例 4：同一密码同一 IV 的输出确定
TEST(CipherTest, DeterministicWithSameKeyAndIv) {
    const std::vector<uint8_t> plain = toBytes("deterministic");
    const std::vector<uint8_t> iv(16, 0x5A);
    EXPECT_EQ(encryptWith("key", iv, plain), encryptWith("key", iv, plain));
}

// 用例 5：口令校验值随口令变化（可用于检测错误密码）
TEST(CipherTest, PasswordCheckDiffersByPassword) {
    const std::vector<uint8_t> iv(16, 0x33);
    StreamCipher good("correct-password");
    good.set_iv(iv);
    StreamCipher bad("wrong-password");
    bad.set_iv(iv);

    EXPECT_NE(good.password_check(), bad.password_check());
}

// 用例 6：同一口令同一 IV 的校验值稳定
TEST(CipherTest, PasswordCheckStable) {
    const std::vector<uint8_t> iv(16, 0x77);
    StreamCipher first("my-password");
    first.set_iv(iv);
    StreamCipher second("my-password");
    second.set_iv(iv);
    EXPECT_EQ(first.password_check(), second.password_check());
}

// 用例 7：密文不含明文片段（抽查文本字节）
TEST(CipherTest, CipherTextHasNoPlainFragment) {
    const std::string text = "CONFIDENTIAL-DATA-BACKUP-CONTENT";
    const std::vector<uint8_t> plain = toBytes(text);
    const std::vector<uint8_t> cipher_text = encryptWith("secret", std::vector<uint8_t>(16, 9), plain);

    ASSERT_GE(cipher_text.size(), text.size());
    // 在密文中搜索明文的 8 字节片段，应当找不到（流密码输出近似随机）
    bool found = false;
    const std::string cipher_string(cipher_text.begin(), cipher_text.end());
    for (size_t i = 0; i + 8 <= text.size(); ++i) {
        if (cipher_string.find(text.substr(i, 8)) != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_FALSE(found);
}

// 用例 8：空输入回环
TEST(CipherTest, EmptyInput) {
    StreamCipher cipher("pwd");
    std::vector<uint8_t> out;
    EXPECT_TRUE(cipher.encrypt({}, out));
    EXPECT_TRUE(out.empty());
}

// 用例 9：大输入（64KB）回环
TEST(CipherTest, LargeInputRoundTrip) {
    std::vector<uint8_t> plain(65536);
    for (size_t i = 0; i < plain.size(); ++i) {
        plain[i] = static_cast<uint8_t>((i * 37 + 11) & 0xFF);
    }
    StreamCipher cipher("big-data-password");
    const std::vector<uint8_t> iv = cipher.iv();
    std::vector<uint8_t> cipher_text;
    ASSERT_TRUE(cipher.encrypt(plain, cipher_text));
    ASSERT_EQ(cipher_text.size(), plain.size());
    EXPECT_EQ(decryptWith("big-data-password", iv, cipher_text), plain);
}

// 用例 10：NoCipher 恒等变换
TEST(CipherTest, NoCipherIsIdentity) {
    const std::vector<uint8_t> plain = toBytes("plain");
    NoCipher cipher;
    std::vector<uint8_t> out;
    EXPECT_TRUE(cipher.encrypt(plain, out));
    EXPECT_EQ(out, plain);
    EXPECT_EQ(cipher.id(), 0);
}

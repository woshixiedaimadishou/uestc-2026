#pragma once

#include <cstdint>
#include <vector>

namespace dataguard {

// LZ77 分词结果
struct Lz77Token {
    enum class Type : uint8_t { Literal, Match };
    Type type = Type::Literal;
    uint8_t literal = 0;    // type == Literal
    uint16_t length = 0;    // type == Match：3..258
    uint16_t distance = 0;  // type == Match：1..32768
};

// LZ77 匹配器：32KB 滑动窗口 + 3 字节哈希链，把字节流切成 token 序列
class Lz77Matcher {
public:
    static constexpr int kWindowSize = 32768;
    static constexpr int kMinMatch = 3;
    static constexpr int kMaxMatch = 258;

    explicit Lz77Matcher(int max_chain = 128) : max_chain_(max_chain) {}

    std::vector<Lz77Token> tokenize(const std::vector<uint8_t>& input) const;

private:
    int max_chain_;
};

}  // namespace dataguard

#include "dataguard/lz77.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace dataguard {
namespace {

constexpr size_t kHashBits = 16;
constexpr size_t kHashSize = 1u << kHashBits;

uint32_t hash3(const std::vector<uint8_t>& data, size_t pos) {
    const uint32_t v = (static_cast<uint32_t>(data[pos]) << 16) |
                       (static_cast<uint32_t>(data[pos + 1]) << 8) |
                       static_cast<uint32_t>(data[pos + 2]);
    return (v * 2654435761u) >> (32 - kHashBits);
}

}  // namespace

std::vector<Lz77Token> Lz77Matcher::tokenize(const std::vector<uint8_t>& input) const {
    std::vector<Lz77Token> tokens;
    const size_t n = input.size();
    tokens.reserve(n / 2 + 1);

    std::vector<int> head(kHashSize, -1);
    std::vector<int> prev(n, -1);

    auto insertPosition = [&](size_t pos) {
        if (pos + kMinMatch > n) return;
        const uint32_t h = hash3(input, pos);
        prev[pos] = head[h];
        head[h] = static_cast<int>(pos);
    };

    size_t pos = 0;
    while (pos < n) {
        size_t best_length = 0;
        size_t best_distance = 0;

        if (pos + static_cast<size_t>(kMinMatch) <= n) {
            const uint32_t h = hash3(input, pos);
            int candidate = head[h];
            int chain = 0;
            const size_t max_match =
                std::min<size_t>(static_cast<size_t>(kMaxMatch), n - pos);

            while (candidate >= 0 && chain < max_chain_) {
                const size_t distance = pos - static_cast<size_t>(candidate);
                if (distance > static_cast<size_t>(kWindowSize)) break;

                size_t length = 0;
                while (length < max_match &&
                       input[static_cast<size_t>(candidate) + length] == input[pos + length]) {
                    ++length;
                }
                if (length > best_length) {
                    best_length = length;
                    best_distance = distance;
                    if (length == max_match) break;
                }
                candidate = prev[static_cast<size_t>(candidate)];
                ++chain;
            }
        }

        insertPosition(pos);

        if (best_length >= static_cast<size_t>(kMinMatch)) {
            Lz77Token token;
            token.type = Lz77Token::Type::Match;
            token.length = static_cast<uint16_t>(best_length);
            token.distance = static_cast<uint16_t>(best_distance);
            tokens.push_back(token);

            // 被匹配覆盖的位置也要进入哈希链，否则后续无法命中
            for (size_t k = 1; k < best_length; ++k) {
                insertPosition(pos + k);
            }
            pos += best_length;
        } else {
            Lz77Token token;
            token.type = Lz77Token::Type::Literal;
            token.literal = input[pos];
            tokens.push_back(token);
            pos += 1;
        }
    }

    return tokens;
}

}  // namespace dataguard

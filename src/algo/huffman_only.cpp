#include "dataguard/huffman_only.h"

#include <array>
#include <cstdint>
#include <queue>
#include <string>
#include <vector>

#include "dataguard/bitstream.h"

namespace dataguard {
namespace {

constexpr uint8_t kVersion = 1;
constexpr int kMaxCodeBits = 15;

struct Node {
    uint64_t freq = 0;
    int symbol = -1;      // 叶子节点保存符号
    int left = -1;
    int right = -1;
};

// 生成码长；返回 false 表示需要退化为定长编码
bool buildCodeLengths(const std::array<uint64_t, 256>& freq,
                      std::array<uint8_t, 256>& lengths) {
    lengths.fill(0);

    struct Item {
        uint64_t freq;
        int order;
        bool operator<(const Item& other) const { return freq > other.freq; }  // 小顶堆
    };

    std::vector<Node> nodes;
    std::priority_queue<Item> heap;
    for (int symbol = 0; symbol < 256; ++symbol) {
        if (freq[static_cast<size_t>(symbol)] == 0) continue;
        nodes.push_back(Node{freq[static_cast<size_t>(symbol)], symbol, -1, -1});
        heap.push(Item{nodes.back().freq, static_cast<int>(nodes.size()) - 1});
    }

    if (nodes.empty()) return true;
    if (nodes.size() == 1) {
        // 只有一种字节：给 1 位码即可（解码按 1 位读）
        lengths[static_cast<size_t>(nodes[0].symbol)] = 1;
        return true;
    }

    while (heap.size() > 1) {
        const Item a = heap.top();
        heap.pop();
        const Item b = heap.top();
        heap.pop();
        Node parent;
        parent.freq = a.freq + b.freq;
        parent.left = a.order;
        parent.right = b.order;
        nodes.push_back(parent);
        heap.push(Item{parent.freq, static_cast<int>(nodes.size()) - 1});
    }

    // 从根向下算深度
    std::vector<int> parents(nodes.size(), -1);
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].left >= 0) parents[static_cast<size_t>(nodes[i].left)] = static_cast<int>(i);
        if (nodes[i].right >= 0) parents[static_cast<size_t>(nodes[i].right)] = static_cast<int>(i);
    }
    int max_bits = 0;
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].symbol < 0) continue;
        int d = 0;
        int cur = parents[i];
        while (cur >= 0) {
            ++d;
            cur = parents[static_cast<size_t>(cur)];
        }
        if (d == 0) d = 1;
        if (d > max_bits) max_bits = d;
        lengths[static_cast<size_t>(nodes[i].symbol)] = static_cast<uint8_t>(d);
    }
    return max_bits <= kMaxCodeBits;
}

// 退化方案：所有出现过的符号统一 8 位（仍是合法前缀码）
void fallbackLengths(const std::array<uint64_t, 256>& freq,
                     std::array<uint8_t, 256>& lengths) {
    lengths.fill(0);
    for (int symbol = 0; symbol < 256; ++symbol) {
        if (freq[static_cast<size_t>(symbol)] > 0) {
            lengths[static_cast<size_t>(symbol)] = 8;
        }
    }
}

// 规范化码字分配
void assignCodes(const std::array<uint8_t, 256>& lengths,
                 std::array<uint16_t, 256>& codes) {
    codes.fill(0);
    std::array<uint32_t, kMaxCodeBits + 1> count{};
    for (int symbol = 0; symbol < 256; ++symbol) {
        const uint8_t len = lengths[static_cast<size_t>(symbol)];
        if (len > 0 && len <= kMaxCodeBits) ++count[len];
    }
    std::array<uint32_t, kMaxCodeBits + 2> next{};
    uint32_t code = 0;
    for (int bits = 1; bits <= kMaxCodeBits; ++bits) {
        code = (code + count[static_cast<size_t>(bits - 1)]) << 1;
        next[static_cast<size_t>(bits)] = code;
    }
    for (int symbol = 0; symbol < 256; ++symbol) {
        const uint8_t len = lengths[static_cast<size_t>(symbol)];
        if (len == 0) continue;
        codes[static_cast<size_t>(symbol)] = static_cast<uint16_t>(next[len]++);
    }
}

// 码长表 RLE 编码：把 256 个长度压成 (len, run) 对
void encodeTable(const std::array<uint8_t, 256>& lengths, std::vector<uint8_t>& out) {
    int index = 0;
    while (index < 256) {
        const uint8_t len = lengths[static_cast<size_t>(index)];
        int run = 1;
        while (index + run < 256 && lengths[static_cast<size_t>(index + run)] == len && run < 255) {
            ++run;
        }
        out.push_back(len);
        out.push_back(static_cast<uint8_t>(run));
        index += run;
    }
}

bool decodeTable(const std::vector<uint8_t>& in, size_t begin, size_t bytes,
                 std::array<uint8_t, 256>& lengths, size_t& consumed) {
    lengths.fill(0);
    size_t pos = begin;
    size_t index = 0;
    const size_t end = begin + bytes;
    if (end > in.size()) return false;
    while (pos + 1 < end) {
        const uint8_t len = in[pos];
        const uint8_t run = in[pos + 1];
        pos += 2;
        if (run == 0 || index + run > 256) return false;
        for (uint8_t i = 0; i < run; ++i) {
            lengths[index++] = len;
        }
    }
    consumed = pos - begin;
    return index == 256;
}

}  // namespace

bool HuffmanOnlyCompressor::compress(const std::vector<uint8_t>& in,
                                     std::vector<uint8_t>& out) const {
    out.clear();
    if (in.empty()) {
        out = {kVersion, 0, 0, 0, 0, 0, 0};
        return true;
    }

    std::array<uint64_t, 256> freq{};
    for (uint8_t byte : in) ++freq[byte];

    std::array<uint8_t, 256> lengths{};
    if (!buildCodeLengths(freq, lengths)) fallbackLengths(freq, lengths);

    std::array<uint16_t, 256> codes{};
    assignCodes(lengths, codes);

    std::vector<uint8_t> table;
    encodeTable(lengths, table);

    out.reserve(7 + table.size() + in.size() / 2);
    out.push_back(kVersion);
    const uint32_t size = static_cast<uint32_t>(in.size());
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((size >> (8 * i)) & 0xFF));
    out.push_back(static_cast<uint8_t>(table.size() & 0xFF));
    out.push_back(static_cast<uint8_t>((table.size() >> 8) & 0xFF));
    out.insert(out.end(), table.begin(), table.end());

    BitWriter writer;
    for (uint8_t byte : in) {
        writer.writeHuffman(codes[byte], lengths[byte]);
    }
    const std::vector<uint8_t> bits = writer.finish();
    out.insert(out.end(), bits.begin(), bits.end());
    return true;
}

bool HuffmanOnlyCompressor::decompress(const std::vector<uint8_t>& in,
                                       std::vector<uint8_t>& out) const {
    out.clear();
    if (in.size() < 7 || in[0] != kVersion) return false;

    const uint32_t size = static_cast<uint32_t>(in[1]) | (static_cast<uint32_t>(in[2]) << 8) |
                          (static_cast<uint32_t>(in[3]) << 16) |
                          (static_cast<uint32_t>(in[4]) << 24);
    const size_t table_bytes = static_cast<size_t>(in[5]) | (static_cast<size_t>(in[6]) << 8);
    if (7 + table_bytes > in.size()) return false;
    if (size == 0) return true;

    std::array<uint8_t, 256> lengths{};
    size_t consumed = 0;
    if (!decodeTable(in, 7, table_bytes, lengths, consumed)) return false;
    if (7 + consumed > in.size()) return false;

    // 规范化解码表：按码长分组，并预先算出每个长度的第一个码字
    std::array<std::vector<uint16_t>, kMaxCodeBits + 1> by_length;
    for (int symbol = 0; symbol < 256; ++symbol) {
        const uint8_t len = lengths[static_cast<size_t>(symbol)];
        if (len > 0 && len <= kMaxCodeBits) {
            by_length[len].push_back(static_cast<uint16_t>(symbol));
        }
    }
    std::array<uint32_t, kMaxCodeBits + 1> first_code{};
    uint32_t running = 0;
    for (int bits = 1; bits <= kMaxCodeBits; ++bits) {
        first_code[static_cast<size_t>(bits)] = running;
        running = (running + static_cast<uint32_t>(by_length[static_cast<size_t>(bits)].size())) << 1;
    }

    const std::vector<uint8_t> payload(in.begin() + static_cast<long>(7 + table_bytes), in.end());
    BitReader reader(payload);
    out.reserve(size);

    while (out.size() < size) {
        uint32_t code = 0;
        bool matched = false;
        for (int bits = 1; bits <= kMaxCodeBits; ++bits) {
            uint32_t bit = 0;
            if (!reader.readHuffman(1, bit)) return false;
            code = (code << 1) | bit;
            const auto& symbols = by_length[static_cast<size_t>(bits)];
            if (symbols.empty()) continue;
            const uint32_t first = first_code[static_cast<size_t>(bits)];
            if (code >= first && code - first < symbols.size()) {
                out.push_back(static_cast<uint8_t>(symbols[code - first]));
                matched = true;
                break;
            }
        }
        if (!matched) return false;
    }
    return true;
}

}  // namespace dataguard

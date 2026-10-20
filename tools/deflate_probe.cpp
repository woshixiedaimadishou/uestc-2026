// DEFLATE 探针：调用项目真实的 LZ77 / Huffman / Deflate 实现，
// 把中间结果（token 流、位流分段、压缩后字节）以 JSON 打印出来，
// 供 tools/deflate_visualize.py 生成可视化页面。不参与正式功能。
//
// 用法：
//   deflate_probe.exe --text "abcabcabcabcabc"
//   deflate_probe.exe --file path/to/file.bin

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "dataguard/deflate.h"
#include "dataguard/huffman.h"
#include "dataguard/lz77.h"

namespace {

// 把字节转成可放进 JSON 字符串的形式：可打印 ASCII 原样输出，其余留空
std::string printableChar(uint8_t byte) {
    if (byte >= 0x20 && byte < 0x7F && byte != '"' && byte != '\\') {
        return std::string(1, static_cast<char>(byte));
    }
    return std::string();
}

std::string hexString(const std::vector<uint8_t>& data) {
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * 2);
    for (uint8_t byte : data) {
        out.push_back(kDigits[byte >> 4]);
        out.push_back(kDigits[byte & 0x0F]);
    }
    return out;
}

struct Segment {
    std::string kind;   // header / lit / lencode / lenextra / distcode / distextra / end / pad
    std::string label;
    size_t bits = 0;
    int token_index = -1;
};

}  // namespace

int main(int argc, char** argv) {
    std::string text;
    std::string file;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--file" && i + 1 < argc) {
            file = argv[++i];
        } else if (arg == "--text" && i + 1 < argc) {
            text = argv[++i];
        }
    }

    if (!file.empty()) {
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            std::cerr << "无法读取文件: " << file << "\n";
            return 1;
        }
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    if (text.empty()) text = "abcabcabcabcabc";

    const std::vector<uint8_t> input(text.begin(), text.end());

    // ---------- 1. LZ77 分词 ----------
    dataguard::Lz77Matcher matcher;
    const std::vector<dataguard::Lz77Token> tokens = matcher.tokenize(input);

    // ---------- 2. 逐 token 计算位流长度 ----------
    std::vector<Segment> segments;
    size_t total_bits = 0;

    segments.push_back({"header", "块头 BFINAL=1 / BTYPE=01", 3, -1});
    total_bits += 3;

    size_t offset = 0;  // 该 token 在原始数据中的字节偏移
    for (size_t i = 0; i < tokens.size(); ++i) {
        const auto& token = tokens[i];
        const int index = static_cast<int>(i);

        if (token.type == dataguard::Lz77Token::Type::Literal) {
            const auto entry = dataguard::FixedHuffmanTable::literalLength(token.literal);
            segments.push_back({"lit", "字面 " + printableChar(token.literal), entry.bits, index});
            total_bits += entry.bits;
            offset += 1;
            continue;
        }

        uint32_t symbol = 0;
        uint32_t extra_bits = 0;
        uint32_t extra_value = 0;
        dataguard::FixedHuffmanTable::encodeLength(token.length, symbol, extra_bits, extra_value);
        const auto length_entry = dataguard::FixedHuffmanTable::literalLength(symbol);

        segments.push_back({"lencode", "长度码 " + std::to_string(symbol), length_entry.bits, index});
        total_bits += length_entry.bits;
        if (extra_bits > 0) {
            segments.push_back({"lenextra", "长度额外 " + std::to_string(extra_bits) + " 位",
                                extra_bits, index});
            total_bits += extra_bits;
        }

        uint32_t dist_symbol = 0;
        uint32_t dist_extra_bits = 0;
        uint32_t dist_extra_value = 0;
        dataguard::FixedHuffmanTable::encodeDistance(token.distance, dist_symbol,
                                                     dist_extra_bits, dist_extra_value);
        segments.push_back({"distcode", "距离码 " + std::to_string(dist_symbol), 5, index});
        total_bits += 5;
        if (dist_extra_bits > 0) {
            segments.push_back({"distextra", "距离额外 " + std::to_string(dist_extra_bits) + " 位",
                                dist_extra_bits, index});
            total_bits += dist_extra_bits;
        }
        offset += token.length;
    }

    segments.push_back({"end", "结束符 256", 7, -1});
    total_bits += 7;

    const size_t pad_bits = (8 - (total_bits % 8)) % 8;
    if (pad_bits > 0) {
        segments.push_back({"pad", "补齐到整字节", pad_bits, -1});
        total_bits += pad_bits;
    }

    // ---------- 3. 真正压缩 ----------
    dataguard::DeflateCompressor deflate;
    std::vector<uint8_t> packed;
    if (!deflate.compress(input, packed)) {
        std::cerr << "压缩失败\n";
        return 1;
    }

    std::vector<uint8_t> restored;
    const bool roundtrip_ok = deflate.decompress(packed, restored) && restored == input;

    // ---------- 4. 输出 JSON ----------
    std::cout << "{";
    std::cout << "\"input_size\":" << input.size() << ",";
    std::cout << "\"compressed_size\":" << packed.size() << ",";
    std::cout << "\"total_bits\":" << total_bits << ",";
    std::cout << "\"roundtrip_ok\":" << (roundtrip_ok ? "true" : "false") << ",";
    std::cout << "\"hex\":\"" << hexString(packed) << "\",";
    std::cout << "\"text\":\"" << hexString(input) << "\",";

    std::cout << "\"tokens\":[";
    offset = 0;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const auto& token = tokens[i];
        if (i > 0) std::cout << ",";
        if (token.type == dataguard::Lz77Token::Type::Literal) {
            std::cout << "{\"kind\":\"lit\",\"byte\":" << static_cast<int>(token.literal)
                      << ",\"char\":\"" << printableChar(token.literal) << "\","
                      << "\"offset\":" << offset << "}";
            offset += 1;
        } else {
            std::cout << "{\"kind\":\"match\",\"length\":" << token.length
                      << ",\"distance\":" << token.distance
                      << ",\"offset\":" << offset << "}";
            offset += token.length;
        }
    }
    std::cout << "],";

    std::cout << "\"segments\":[";
    for (size_t i = 0; i < segments.size(); ++i) {
        if (i > 0) std::cout << ",";
        std::cout << "{\"kind\":\"" << segments[i].kind << "\","
                  << "\"label\":\"" << segments[i].label << "\","
                  << "\"bits\":" << segments[i].bits << ","
                  << "\"token\":" << segments[i].token_index << "}";
    }
    std::cout << "]}";
    std::cout << "\n";
    return 0;
}

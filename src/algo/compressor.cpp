#include "dataguard/compressor.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "dataguard/deflate.h"
#include "dataguard/huffman_only.h"
#include "dataguard/lz77_only.h"

namespace dataguard {

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

Codec codecFromName(const std::string& name) {
    const std::string key = lowerCopy(name);
    if (key.empty() || key == "none" || key == "off") return Codec::None;
    if (key == "deflate" || key == "lz77+huffman" || key == "huffman+lz77") return Codec::Deflate;
    if (key == "huffman" || key == "huffman-only") return Codec::HuffmanOnly;
    if (key == "lz77" || key == "lz" || key == "lz77-only") return Codec::Lz77Only;
    return Codec::None;
}

const char* codecName(Codec codec) {
    switch (codec) {
        case Codec::Deflate: return "deflate";
        case Codec::HuffmanOnly: return "huffman";
        case Codec::Lz77Only: return "lz77";
        default: return "none";
    }
}

uint8_t codecId(Codec codec) {
    switch (codec) {
        case Codec::Deflate: return kCompressorDeflate;
        case Codec::HuffmanOnly: return kCompressorHuffman;
        case Codec::Lz77Only: return kCompressorLz77;
        default: return kCompressorNone;
    }
}

Codec codecFromId(uint8_t id) {
    switch (id) {
        case kCompressorDeflate: return Codec::Deflate;
        case kCompressorHuffman: return Codec::HuffmanOnly;
        case kCompressorLz77: return Codec::Lz77Only;
        default: return Codec::None;
    }
}

std::unique_ptr<Compressor> makeCompressor(Codec codec) {
    switch (codec) {
        case Codec::Deflate: return std::make_unique<DeflateCompressor>();
        case Codec::HuffmanOnly: return std::make_unique<HuffmanOnlyCompressor>();
        case Codec::Lz77Only: return std::make_unique<Lz77OnlyCompressor>();
        case Codec::None: return std::make_unique<NoCompressor>();
    }
    return nullptr;
}

}  // namespace dataguard

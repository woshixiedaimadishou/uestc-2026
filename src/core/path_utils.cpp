#include "dataguard/path_utils.h"

#include <string>

namespace dataguard {
namespace {

// 单字符大小写不敏感比较（仅处理 ASCII 字母）
char lowerChar(char c) {
    if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
    return c;
}

}  // namespace

std::string toLowerAscii(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out.push_back(lowerChar(c));
    return out;
}

bool glob_match(const std::string& pattern, const std::string& text) {
    // 迭代回溯算法：遇到 '*' 记录位置，失配时回退到最近的 '*'
    const size_t n = pattern.size();
    const size_t m = text.size();
    size_t pi = 0;
    size_t ti = 0;
    size_t star = std::string::npos;
    size_t mark = 0;

    while (ti < m) {
        if (pi < n && (pattern[pi] == '?' || lowerChar(pattern[pi]) == lowerChar(text[ti]))) {
            ++pi;
            ++ti;
        } else if (pi < n && pattern[pi] == '*') {
            star = pi;
            mark = ti;
            ++pi;
        } else if (star != std::string::npos) {
            pi = star + 1;
            ti = ++mark;
        } else {
            return false;
        }
    }

    // 尾部允许剩余的 '*'
    while (pi < n && pattern[pi] == '*') ++pi;
    return pi == n;
}

std::string pathToUtf8(const std::filesystem::path& p) {
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

std::filesystem::path utf8ToPath(const std::string& s) {
    return std::filesystem::u8path(s);
}

}  // namespace dataguard

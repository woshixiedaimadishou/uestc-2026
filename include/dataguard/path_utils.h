#pragma once

#include <filesystem>
#include <string>

namespace dataguard {

// 通配符匹配：'*' 匹配任意长度（含空串），'?' 匹配单个字符；大小写不敏感（Windows 语义）。
// 用于筛选规则的路径/文件名匹配。
bool glob_match(const std::string& pattern, const std::string& text);

// ASCII 大写转小写（用于大小写不敏感比较；中文 UTF-8 字节不受影响）
std::string toLowerAscii(const std::string& s);

// std::filesystem::path 与 UTF-8 字符串互转（保证中文路径跨模块一致）
std::string pathToUtf8(const std::filesystem::path& p);
std::filesystem::path utf8ToPath(const std::string& s);

}  // namespace dataguard

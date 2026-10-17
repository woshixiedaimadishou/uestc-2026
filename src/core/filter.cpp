#include "dataguard/filter.h"

#include <string>

#include "dataguard/path_utils.h"

namespace dataguard {
namespace {

// 取相对路径的文件名部分（最后一个 '/' 之后）
std::string nameOf(const std::string& rel) {
    const size_t slash = rel.find_last_of('/');
    return (slash == std::string::npos) ? rel : rel.substr(slash + 1);
}

// 取扩展名（含点，小写）；文件名中没有 '.' 时返回空串
std::string extensionOf(const std::string& rel) {
    const std::string name = nameOf(rel);
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0) return std::string();
    return toLowerAscii(name.substr(dot));
}

bool containsIgnoreCase(const std::vector<std::string>& list, const std::string& value) {
    const std::string lower = toLowerAscii(value);
    for (const auto& item : list) {
        if (toLowerAscii(item) == lower) return true;
    }
    return false;
}

}  // namespace

bool Filter::matches(const FileEntry& entry) const {
    if (entry.kind == FileKind::Directory) return true;

    const std::string& rel = entry.rel_path_utf8;
    return matchesPath(rel) && matchesExt(rel) && matchesName(rel) &&
           matchesTime(entry.mtime) && matchesSize(entry.size) &&
           matchesOwner(entry.owner);
}

bool Filter::matchesPath(const std::string& rel) const {
    for (const auto& pattern : spec_.exclude_path_globs) {
        if (glob_match(pattern, rel)) return false;
    }
    if (spec_.include_path_globs.empty()) return true;
    for (const auto& pattern : spec_.include_path_globs) {
        if (glob_match(pattern, rel)) return true;
    }
    return false;
}

bool Filter::matchesExt(const std::string& rel) const {
    const std::string ext = extensionOf(rel);
    if (!spec_.exclude_exts.empty() && containsIgnoreCase(spec_.exclude_exts, ext)) return false;
    if (!spec_.include_exts.empty()) return containsIgnoreCase(spec_.include_exts, ext);
    return true;
}

bool Filter::matchesName(const std::string& rel) const {
    const std::string name = nameOf(rel);
    for (const auto& pattern : spec_.exclude_name_globs) {
        if (glob_match(pattern, name)) return false;
    }
    if (spec_.include_name_globs.empty()) return true;
    for (const auto& pattern : spec_.include_name_globs) {
        if (glob_match(pattern, name)) return true;
    }
    return false;
}

bool Filter::matchesTime(int64_t mtime) const {
    if (spec_.modified_after >= 0 && mtime < spec_.modified_after) return false;
    if (spec_.modified_before >= 0 && mtime > spec_.modified_before) return false;
    return true;
}

bool Filter::matchesSize(uint64_t size) const {
    if (spec_.min_size >= 0 && static_cast<int64_t>(size) < spec_.min_size) return false;
    if (spec_.max_size >= 0 && static_cast<int64_t>(size) > spec_.max_size) return false;
    return true;
}

bool Filter::matchesOwner(const std::string& owner) const {
    for (const auto& item : spec_.exclude_owners) {
        if (toLowerAscii(item) == toLowerAscii(owner)) return false;
    }
    if (spec_.include_owners.empty()) return true;
    return containsIgnoreCase(spec_.include_owners, owner);
}

}  // namespace dataguard

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "dataguard/file_entry.h"

namespace dataguard {

// 六类筛选条件（README 自定义备份评分项）：
// 路径 / 类型（扩展名） / 名字 / 修改时间 / 尺寸 / 属主
struct FilterSpec {
    // 路径 glob（相对源根目录，'/' 分隔）；为空表示不限制
    std::vector<std::string> include_path_globs;
    std::vector<std::string> exclude_path_globs;

    // 扩展名（含点，如 ".cpp"），比较时统一转小写
    std::vector<std::string> include_exts;
    std::vector<std::string> exclude_exts;

    // 文件名 glob（不含路径）
    std::vector<std::string> include_name_globs;
    std::vector<std::string> exclude_name_globs;

    // 修改时间范围（Unix 秒）；-1 表示不限制
    int64_t modified_after = -1;
    int64_t modified_before = -1;

    // 尺寸范围（字节）；-1 表示不限制
    int64_t min_size = -1;
    int64_t max_size = -1;

    // 属主用户名（不区分大小写）
    std::vector<std::string> include_owners;
    std::vector<std::string> exclude_owners;

    // 扫描时是否跟随符号链接（由 FileScanner 使用）
    bool follow_symlinks = false;
};

// 筛选器：判断单个条目是否应被备份。规则优先级：exclude 优先于 include。
class Filter {
public:
    explicit Filter(const FilterSpec& spec) : spec_(spec) {}

    // 目录恒返回 true（目录是否遍历由 scanner 控制）；普通文件/链接按规则判断
    bool matches(const FileEntry& entry) const;

    const FilterSpec& spec() const { return spec_; }
    bool followSymlinks() const { return spec_.follow_symlinks; }

private:
    bool matchesPath(const std::string& rel) const;
    bool matchesExt(const std::string& rel) const;
    bool matchesName(const std::string& rel) const;
    bool matchesTime(int64_t mtime) const;
    bool matchesSize(uint64_t size) const;
    bool matchesOwner(const std::string& owner) const;

    FilterSpec spec_;
};

}  // namespace dataguard

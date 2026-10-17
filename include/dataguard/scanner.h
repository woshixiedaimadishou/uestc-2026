#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "dataguard/file_entry.h"
#include "dataguard/filter.h"

namespace dataguard {

// 文件扫描器：递归遍历目录树，产出满足筛选条件的条目列表。
// 约定：
//   - 目录条目先于其子项输出（便于还原时先创建目录）
//   - 同级条目按文件名排序，保证扫描结果确定性
//   - 符号链接默认不跟随（除非 FilterSpec::follow_symlinks 为 true）
//   - 无法访问的子目录/文件通过 on_error 回调报告，不中断整体扫描
class FileScanner {
public:
    explicit FileScanner(const Filter& filter) : filter_(filter) {}

    std::vector<FileEntry> scan(
        const std::filesystem::path& root,
        const std::function<void(const FileEntry&)>& on_entry = {},
        const std::function<void(const std::string&)>& on_error = {}) const;

private:
    void scanDir(const std::filesystem::path& dir,
                 const std::string& rel_prefix,
                 std::vector<FileEntry>& out,
                 const std::function<void(const FileEntry&)>& on_entry,
                 const std::function<void(const std::string&)>& on_error) const;

    const Filter& filter_;
};

}  // namespace dataguard

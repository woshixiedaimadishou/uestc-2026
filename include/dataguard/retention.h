#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dataguard {

// 数据淘汰策略（由用户设置，0 表示不启用该维度）
struct RetentionOptions {
    int keep_count = 0;  // 保留最近 N 份档案
    int keep_days = 0;   // 保留最近 N 天内的档案
};

struct RetentionResult {
    std::vector<std::filesystem::path> removed;
    uint64_t failed = 0;
    uint64_t kept = 0;
};

// 扫描 dest_dir 下形如 <task_name>_*.bkpk 的档案，按修改时间由新到旧排序，
// 依据 keep_count / keep_days 删除过期档案。
// log_lines 非空时追加人类可读日志（由 CLI 转为行协议输出）。
RetentionResult applyRetention(const std::filesystem::path& dest_dir,
                               const std::string& task_name,
                               const RetentionOptions& options,
                               std::vector<std::string>* log_lines = nullptr);

}  // namespace dataguard

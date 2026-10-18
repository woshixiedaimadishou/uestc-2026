#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "dataguard/archive_writer.h"
#include "dataguard/filter.h"

namespace dataguard {

enum class LogLevel { Info, Warn, Error };

// 进度与日志回调：GUI 端通过 CLI 行协议获取，单元测试可注入自定义实现
struct ProgressReporter {
    virtual ~ProgressReporter() = default;
    virtual void on_file_start(const std::string& rel_path,
                               uint64_t file_index,
                               uint64_t total_files) = 0;
    virtual void on_file_progress(uint64_t bytes_done, uint64_t file_size) = 0;
    virtual void on_log(LogLevel level, const std::string& message) = 0;
};

// 备份任务配置（定时计划与保留策略在 Python 端，不在此结构）
struct BackupTaskConfig {
    std::string name = "backup";
    std::filesystem::path source;
    std::filesystem::path dest_dir;
    FilterSpec filter;
    ArchiveOptions options;
};

// 备份执行器：扫描 → 过滤 → 写档案
class BackupManager {
public:
    bool run(const BackupTaskConfig& cfg, ProgressReporter* reporter = nullptr);

    const std::string& last_error() const { return last_error_; }
    std::filesystem::path last_archive() const { return last_archive_; }
    uint64_t last_success_count() const { return last_success_count_; }
    uint64_t last_failed_count() const { return last_failed_count_; }

private:
    std::string last_error_;
    std::filesystem::path last_archive_;
    uint64_t last_success_count_ = 0;
    uint64_t last_failed_count_ = 0;
};

// 还原执行器：打开档案 → 逐条解出到目标目录
class RestoreManager {
public:
    bool run(const std::filesystem::path& archive,
             const std::filesystem::path& dest,
             const std::string& password,
             bool overwrite = true,
             const std::vector<std::string>& only_files = {},
             ProgressReporter* reporter = nullptr);

    const std::string& last_error() const { return last_error_; }
    uint64_t last_success_count() const { return last_success_count_; }
    uint64_t last_failed_count() const { return last_failed_count_; }

private:
    std::string last_error_;
    uint64_t last_success_count_ = 0;
    uint64_t last_failed_count_ = 0;
};

}  // namespace dataguard

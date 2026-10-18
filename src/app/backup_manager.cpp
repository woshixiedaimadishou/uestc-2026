#include "dataguard/backup_manager.h"

#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "dataguard/archive_reader.h"
#include "dataguard/path_utils.h"
#include "dataguard/scanner.h"

namespace dataguard {
namespace {

std::string nowStamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local = {};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::ostringstream oss;
    oss << std::put_time(&local, "%Y%m%d_%H%M%S");
    return oss.str();
}

std::string sanitizeName(const std::string& name) {
    if (name.empty()) return "backup";
    std::string out;
    for (char c : name) {
        const bool bad = c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
                         c == '"' || c == '<' || c == '>' || c == '|';
        out.push_back(bad ? '_' : c);
    }
    return out;
}

}  // namespace

bool BackupManager::run(const BackupTaskConfig& cfg, ProgressReporter* reporter) {
    last_error_.clear();
    last_archive_.clear();
    last_success_count_ = 0;
    last_failed_count_ = 0;

    std::error_code ec;
    if (!std::filesystem::exists(cfg.source, ec)) {
        last_error_ = "源目录不存在: " + pathToUtf8(cfg.source);
        if (reporter) reporter->on_log(LogLevel::Error, last_error_);
        return false;
    }
    std::filesystem::create_directories(cfg.dest_dir, ec);
    if (ec) {
        last_error_ = "无法创建目标目录: " + pathToUtf8(cfg.dest_dir);
        if (reporter) reporter->on_log(LogLevel::Error, last_error_);
        return false;
    }

    const std::string name = sanitizeName(cfg.name);
    const std::filesystem::path archive =
        cfg.dest_dir / utf8ToPath(name + "_" + nowStamp() + ".bkpk");

    // 1. 扫描
    Filter filter(cfg.filter);
    FileScanner scanner(filter);
    std::vector<FileEntry> entries = scanner.scan(
        cfg.source, {},
        [reporter](const std::string& message) {
            if (reporter) reporter->on_log(LogLevel::Warn, message);
        });

    // 2. 写档案
    ArchiveWriter writer;
    if (!writer.open(archive, cfg.options)) {
        last_error_ = writer.last_error();
        if (reporter) reporter->on_log(LogLevel::Error, last_error_);
        return false;
    }

    const uint64_t total = static_cast<uint64_t>(entries.size());
    uint64_t success = 0;
    uint64_t failed = 0;
    for (uint64_t i = 0; i < total; ++i) {
        const FileEntry& entry = entries[static_cast<size_t>(i)];
        if (reporter) reporter->on_file_start(entry.rel_path_utf8, i + 1, total);
        if (writer.add(entry)) {
            ++success;
        } else {
            ++failed;
            if (reporter) reporter->on_log(LogLevel::Warn, writer.last_error());
        }
    }

    if (!writer.close()) {
        last_error_ = writer.last_error();
        if (reporter) reporter->on_log(LogLevel::Error, last_error_);
        return false;
    }

    if (success == 0 && failed > 0) {
        last_error_ = "没有文件被成功备份";
        if (reporter) reporter->on_log(LogLevel::Error, last_error_);
        return false;
    }

    last_archive_ = archive;
    last_success_count_ = success;
    last_failed_count_ = failed;
    if (reporter && failed > 0) {
        reporter->on_log(LogLevel::Warn,
                         "部分文件备份失败: " + std::to_string(failed) + " 个");
    }
    return true;
}

bool RestoreManager::run(const std::filesystem::path& archive,
                         const std::filesystem::path& dest,
                         const std::string& password,
                         bool overwrite,
                         const std::vector<std::string>& only_files,
                         ProgressReporter* reporter) {
    last_error_.clear();
    last_success_count_ = 0;
    last_failed_count_ = 0;

    ArchiveReader reader;
    if (!reader.open(archive, password)) {
        last_error_ = reader.last_error();
        if (reporter) reporter->on_log(LogLevel::Error, last_error_);
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(dest, ec);
    if (ec) {
        last_error_ = "无法创建还原目标目录: " + pathToUtf8(dest);
        if (reporter) reporter->on_log(LogLevel::Error, last_error_);
        return false;
    }

    const auto& entries = reader.entries();
    const uint64_t total = static_cast<uint64_t>(entries.size());
    uint64_t restored = 0;
    uint64_t failed = 0;

    for (uint64_t i = 0; i < total; ++i) {
        const FileEntry& entry = entries[static_cast<size_t>(i)];

        if (!only_files.empty()) {
            bool selected = false;
            for (const auto& rel : only_files) {
                if (rel == entry.rel_path_utf8) {
                    selected = true;
                    break;
                }
            }
            if (!selected) continue;
        }

        if (reporter) reporter->on_file_start(entry.rel_path_utf8, i + 1, total);

        if (!overwrite && entry.kind == FileKind::File) {
            const std::filesystem::path target = dest / utf8ToPath(entry.rel_path_utf8);
            if (std::filesystem::exists(target, ec)) {
                if (reporter) {
                    reporter->on_log(LogLevel::Warn, "跳过已存在文件: " + entry.rel_path_utf8);
                }
                continue;
            }
        }

        if (reader.extract(entry, dest)) {
            ++restored;
        } else {
            ++failed;
            if (reporter) reporter->on_log(LogLevel::Warn, reader.last_error());
        }
    }

    if (failed > 0) {
        last_error_ = "部分条目还原失败: " + std::to_string(failed) + " 个";
    }
    last_success_count_ = restored;
    last_failed_count_ = failed;
    return failed == 0 || restored > 0;
}

}  // namespace dataguard

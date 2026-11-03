#include "dataguard/backup_manager.h"

#include <algorithm>
#include <atomic>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
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

    // 2. 预先标记硬链接引用（与写入器规则一致），避免对重复文件做无谓的读盘与压缩
    {
        std::unordered_map<uint64_t, std::string> seen;
        for (auto& entry : entries) {
            if (entry.kind != FileKind::File || entry.hard_link_count <= 1 || entry.file_id == 0) {
                continue;
            }
            const auto it = seen.find(entry.file_id);
            if (it != seen.end()) {
                entry.kind = FileKind::Hardlink;
                entry.link_target_utf8 = it->second;
                entry.size = 0;
            } else {
                seen.emplace(entry.file_id, entry.rel_path_utf8);
            }
        }
    }

    // 3. 并行准备（读文件 + 算 CRC + 压缩），登记仍按原顺序串行进行
    ArchiveWriter writer;
    if (!writer.open(archive, cfg.options)) {
        last_error_ = writer.last_error();
        if (reporter) reporter->on_log(LogLevel::Error, last_error_);
        return false;
    }

    const uint64_t total = static_cast<uint64_t>(entries.size());
    std::vector<PreparedEntry> prepared(total);

    unsigned workers = std::thread::hardware_concurrency();
    if (workers == 0) workers = 1;
    workers = std::min(workers, 8u);
    if (total < 64) workers = 1;  // 小任务不值得起线程

    std::atomic<size_t> next_index{0};
    std::atomic<size_t> ready_count{0};
    auto prepareWorker = [&]() {
        while (true) {
            const size_t index = next_index.fetch_add(1);
            if (index >= total) break;
            ArchiveWriter::prepare(entries[index], cfg.options, prepared[index]);
            const size_t done = ready_count.fetch_add(1) + 1;
            if (reporter && workers > 1 && done % 500 == 0) {
                reporter->on_log(LogLevel::Info,
                                 "已读取 " + std::to_string(done) + "/" + std::to_string(total));
            }
        }
    };

    if (workers <= 1) {
        prepareWorker();
    } else {
        std::vector<std::thread> pool;
        pool.reserve(workers - 1);
        for (unsigned i = 1; i < workers; ++i) pool.emplace_back(prepareWorker);
        prepareWorker();
        for (auto& thread : pool) thread.join();
    }

    // 4. 顺序登记（保证条目顺序、偏移与硬链接指向确定）
    uint64_t success = 0;
    uint64_t failed = 0;
    for (uint64_t i = 0; i < total; ++i) {
        PreparedEntry& item = prepared[static_cast<size_t>(i)];
        if (reporter) reporter->on_file_start(entries[static_cast<size_t>(i)].rel_path_utf8,
                                              i + 1, total);
        if (item.ok && writer.addPrepared(std::move(item))) {
            ++success;
        } else {
            ++failed;
            const std::string message = item.error.empty() ? writer.last_error() : item.error;
            if (reporter) reporter->on_log(LogLevel::Warn, message);
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
            // 选中文件 → 只还原该条目；选中目录 → 还原其整棵子树
            bool selected = false;
            for (const auto& rel : only_files) {
                if (rel == entry.rel_path_utf8 ||
                    (entry.rel_path_utf8.size() > rel.size() &&
                     entry.rel_path_utf8.compare(0, rel.size(), rel) == 0 &&
                     entry.rel_path_utf8[rel.size()] == '/')) {
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
    if (!only_files.empty() && restored == 0 && failed == 0) {
        last_error_ = "选中的条目在档案中不存在（共 " + std::to_string(only_files.size()) + " 项）";
        if (reporter) reporter->on_log(LogLevel::Error, last_error_);
        last_success_count_ = 0;
        last_failed_count_ = 0;
        return false;
    }
    last_success_count_ = restored;
    last_failed_count_ = failed;
    return failed == 0 || restored > 0;
}

}  // namespace dataguard

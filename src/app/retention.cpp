#include "dataguard/retention.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "dataguard/path_utils.h"

namespace dataguard {
namespace {

bool startsWith(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() &&
           text.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::time_t fileMtime(const std::filesystem::path& path) {
    std::error_code ec;
    const auto ft = std::filesystem::last_write_time(path, ec);
    if (ec) return 0;
    const auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ft - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::system_clock::to_time_t(sctp);
}

}  // namespace

RetentionResult applyRetention(const std::filesystem::path& dest_dir,
                               const std::string& task_name,
                               const RetentionOptions& options,
                               std::vector<std::string>* log_lines) {
    RetentionResult result;

    std::error_code ec;
    if (!std::filesystem::is_directory(dest_dir, ec)) {
        if (log_lines) log_lines->push_back("目标目录不存在: " + pathToUtf8(dest_dir));
        return result;
    }

    const std::string prefix = task_name + "_";
    std::vector<std::pair<std::filesystem::path, std::time_t>> archives;

    for (std::filesystem::directory_iterator it(
             dest_dir, std::filesystem::directory_options::skip_permission_denied, ec);
         !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string name = pathToUtf8(it->path().filename());
        if (!startsWith(name, prefix) || !endsWith(name, ".bkpk")) continue;
        archives.emplace_back(it->path(), fileMtime(it->path()));
    }

    // 由新到旧排序（mtime 降序，相同则按路径降序保证稳定性）
    std::sort(archives.begin(), archives.end(),
              [](const auto& a, const auto& b) {
                  if (a.second != b.second) return a.second > b.second;
                  return pathToUtf8(a.first) > pathToUtf8(b.first);
              });

    const std::time_t now = std::time(nullptr);
    for (size_t index = 0; index < archives.size(); ++index) {
        const auto& item = archives[index];
        bool should_remove = false;

        if (options.keep_count > 0 && index >= static_cast<size_t>(options.keep_count)) {
            should_remove = true;
        }
        if (options.keep_days > 0) {
            const double age_days = std::difftime(now, item.second) / 86400.0;
            if (age_days > static_cast<double>(options.keep_days)) {
                should_remove = true;
            }
        }

        if (!should_remove) {
            ++result.kept;
            continue;
        }

        std::error_code remove_ec;
        if (std::filesystem::remove(item.first, remove_ec)) {
            result.removed.push_back(item.first);
            if (log_lines) log_lines->push_back("已淘汰: " + pathToUtf8(item.first));
        } else {
            ++result.failed;
            if (log_lines) {
                log_lines->push_back("淘汰失败: " + pathToUtf8(item.first));
            }
        }
    }

    return result;
}

}  // namespace dataguard

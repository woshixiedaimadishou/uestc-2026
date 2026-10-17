#include "dataguard/scanner.h"

#include <algorithm>
#include <string>
#include <system_error>
#include <vector>

#include "dataguard/path_utils.h"
#include "dataguard/win_metadata.h"

namespace dataguard {

std::vector<FileEntry> FileScanner::scan(
    const std::filesystem::path& root,
    const std::function<void(const FileEntry&)>& on_entry,
    const std::function<void(const std::string&)>& on_error) const {
    std::vector<FileEntry> result;

    std::error_code ec;
    const auto status = std::filesystem::status(root, ec);
    if (ec || !std::filesystem::exists(status)) {
        if (on_error) on_error("源目录不存在或无法访问: " + pathToUtf8(root));
        return result;
    }
    if (!std::filesystem::is_directory(status)) {
        if (on_error) on_error("源路径不是目录: " + pathToUtf8(root));
        return result;
    }

    scanDir(root, std::string(), result, on_entry, on_error);
    return result;
}

void FileScanner::scanDir(
    const std::filesystem::path& dir,
    const std::string& rel_prefix,
    std::vector<FileEntry>& out,
    const std::function<void(const FileEntry&)>& on_entry,
    const std::function<void(const std::string&)>& on_error) const {
    std::vector<std::filesystem::directory_entry> children;
    std::error_code ec;

    std::filesystem::directory_iterator it(
        dir, std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec) {
        if (on_error) on_error("无法读取目录: " + pathToUtf8(dir));
        return;
    }
    for (const auto& entry : it) {
        children.push_back(entry);
    }

    // 按文件名排序，保证扫描结果确定性
    std::sort(children.begin(), children.end(),
              [](const std::filesystem::directory_entry& a,
                 const std::filesystem::directory_entry& b) {
                  return pathToUtf8(a.path().filename()) < pathToUtf8(b.path().filename());
              });

    for (const auto& child : children) {
        const std::filesystem::path child_path = child.path();
        const std::string name = pathToUtf8(child_path.filename());
        const std::string rel = rel_prefix.empty() ? name : rel_prefix + "/" + name;

        FileEntry entry;
        std::string err;
        if (!readFileMetadata(child_path, entry, &err)) {
            if (on_error) on_error(err.empty() ? ("无法读取: " + rel) : err);
            continue;
        }
        entry.rel_path_utf8 = rel;

        if (entry.kind == FileKind::Directory) {
            out.push_back(entry);
            if (on_entry) on_entry(entry);
            scanDir(child_path, rel, out, on_entry, on_error);
        } else if (filter_.matches(entry)) {
            out.push_back(entry);
            if (on_entry) on_entry(entry);
        }
    }
}

}  // namespace dataguard

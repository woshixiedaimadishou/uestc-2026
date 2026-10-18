#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

#include "dataguard/archive_reader.h"
#include "dataguard/backup_manager.h"
#include "dataguard/path_utils.h"

namespace {

constexpr const char* kProductName = "DataGuard";
constexpr const char* kVersion = "0.2.0";

std::string wideToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int size = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) return std::string();
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                        out.data(), size, nullptr, nullptr);
    return out;
}

// 通过宽字符命令行解析参数，保证中文路径不乱码
std::vector<std::string> utf8Args() {
    std::vector<std::string> args;
    int argc = 0;
    LPWSTR* argvw = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argvw == nullptr) return args;
    for (int i = 0; i < argc; ++i) args.push_back(wideToUtf8(argvw[i]));
    LocalFree(argvw);
    return args;
}

// 行协议输出（供 Python GUI 与集成脚本解析）
class ConsoleReporter : public dataguard::ProgressReporter {
public:
    void on_file_start(const std::string& rel_path,
                       uint64_t file_index,
                       uint64_t total_files) override {
        std::cout << "PROGRESS\t" << file_index << "\t" << total_files << "\t" << rel_path
                  << "\n";
    }

    void on_file_progress(uint64_t bytes_done, uint64_t file_size) override {
        if (file_size == 0) return;
        std::cout << "PROGRESS_B\t0\t" << bytes_done << "\t" << file_size << "\n";
    }

    void on_log(dataguard::LogLevel level, const std::string& message) override {
        const char* name = "INFO";
        if (level == dataguard::LogLevel::Warn) name = "WARN";
        if (level == dataguard::LogLevel::Error) name = "ERROR";
        std::cout << "LOG\t" << name << "\t" << message << "\n";
    }
};

void printDone(uint64_t success, uint64_t failed, const std::string& archive) {
    std::cout << "DONE\t" << success << "\t" << failed << "\t" << archive << "\n";
}

void printUsage() {
    std::cout
        << kProductName << " " << kVersion << " - 数据备份软件命令行\n\n"
        << "用法:\n"
        << "  dataguard-cli backup <源目录> <目标目录> [选项]\n"
        << "  dataguard-cli restore <档案> <目标目录> [选项]\n"
        << "  dataguard-cli list <档案> [--password P]\n"
        << "  dataguard-cli verify <档案> [--password P]\n"
        << "  dataguard-cli --version | --help\n\n"
        << "backup 选项:\n"
        << "  --name <名称>            档案名前缀（默认 backup）\n"
        << "  --compress               启用压缩（v0.3 提供）\n"
        << "  --encrypt                启用加密（v0.4 提供）\n"
        << "  --password <密码>        加密口令\n"
        << "  --include-ext <.ext>     仅包含指定扩展名（可多次）\n"
        << "  --exclude-ext <.ext>     排除指定扩展名（可多次）\n"
        << "  --include-glob <模式>    路径包含 glob（可多次）\n"
        << "  --exclude-glob <模式>    路径排除 glob（可多次）\n"
        << "  --min-size <字节>        最小文件尺寸\n"
        << "  --max-size <字节>        最大文件尺寸\n"
        << "  --modified-after <日期>  仅备份该日期(YYYY-MM-DD)之后修改的文件\n"
        << "  --owner <用户名>         仅备份指定属主的文件（可多次）\n"
        << "  --follow-symlinks        跟随符号链接目录\n\n"
        << "restore 选项:\n"
        << "  --password <密码>        档案口令\n"
        << "  --files a.txt,dir/b.txt  仅还原指定条目（逗号分隔相对路径）\n"
        << "  --no-overwrite           不覆盖已存在的文件\n\n"
        << "说明: backup/restore 的 stdout 为行协议（PROGRESS/LOG/DONE），\n"
        << "      供图形界面与集成测试解析。\n";
}

int64_t parseDate(const std::string& text) {
    int year = 0;
    int month = 0;
    int day = 0;
    if (std::sscanf(text.c_str(), "%d-%d-%d", &year, &month, &day) != 3) return -1;
    std::tm tm = {};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    tm.tm_isdst = -1;
    const std::time_t t = std::mktime(&tm);
    return static_cast<int64_t>(t);
}

std::string formatTime(int64_t unix_seconds) {
    if (unix_seconds <= 0) return "-";
    const std::time_t t = static_cast<std::time_t>(unix_seconds);
    std::tm local = {};
#if defined(_WIN32)
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char buf[32] = {};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &local);
    return std::string(buf);
}

const char* kindName(dataguard::FileKind kind) {
    switch (kind) {
        case dataguard::FileKind::File: return "文件";
        case dataguard::FileKind::Directory: return "目录";
        case dataguard::FileKind::Symlink: return "符号链接";
        case dataguard::FileKind::Hardlink: return "硬链接";
        case dataguard::FileKind::Other: return "其他";
    }
    return "未知";
}

int cmdBackup(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        std::cerr << "用法: dataguard-cli backup <源目录> <目标目录> [选项]\n";
        return 1;
    }

    dataguard::BackupTaskConfig cfg;
    cfg.source = dataguard::utf8ToPath(args[1]);
    cfg.dest_dir = dataguard::utf8ToPath(args[2]);

    for (size_t i = 3; i < args.size(); ++i) {
        const std::string& opt = args[i];
        const bool has_value = (i + 1 < args.size());
        if (opt == "--name" && has_value) {
            cfg.name = args[++i];
        } else if (opt == "--compress") {
            cfg.options.compress = true;
        } else if (opt == "--encrypt") {
            cfg.options.encrypt = true;
        } else if (opt == "--password" && has_value) {
            cfg.options.password = args[++i];
        } else if (opt == "--include-ext" && has_value) {
            cfg.filter.include_exts.push_back(args[++i]);
        } else if (opt == "--exclude-ext" && has_value) {
            cfg.filter.exclude_exts.push_back(args[++i]);
        } else if (opt == "--include-glob" && has_value) {
            cfg.filter.include_path_globs.push_back(args[++i]);
        } else if (opt == "--exclude-glob" && has_value) {
            cfg.filter.exclude_path_globs.push_back(args[++i]);
        } else if (opt == "--min-size" && has_value) {
            cfg.filter.min_size = std::stoll(args[++i]);
        } else if (opt == "--max-size" && has_value) {
            cfg.filter.max_size = std::stoll(args[++i]);
        } else if (opt == "--modified-after" && has_value) {
            cfg.filter.modified_after = parseDate(args[++i]);
            if (cfg.filter.modified_after < 0) {
                std::cerr << "日期格式错误，应为 YYYY-MM-DD\n";
                return 1;
            }
        } else if (opt == "--owner" && has_value) {
            cfg.filter.include_owners.push_back(args[++i]);
        } else if (opt == "--follow-symlinks") {
            cfg.filter.follow_symlinks = true;
        } else {
            std::cerr << "未知参数: " << opt << "\n";
            return 1;
        }
    }

    ConsoleReporter reporter;
    dataguard::BackupManager manager;
    const bool ok = manager.run(cfg, &reporter);
    if (!ok) {
        std::cout << "LOG\tERROR\t" << manager.last_error() << "\n";
        printDone(manager.last_success_count(), manager.last_failed_count(), "");
        return 1;
    }
    printDone(manager.last_success_count(), manager.last_failed_count(),
              dataguard::pathToUtf8(manager.last_archive()));
    return 0;
}

int cmdRestore(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        std::cerr << "用法: dataguard-cli restore <档案> <目标目录> [选项]\n";
        return 1;
    }

    std::string password;
    bool overwrite = true;
    std::vector<std::string> only_files;

    for (size_t i = 3; i < args.size(); ++i) {
        const std::string& opt = args[i];
        const bool has_value = (i + 1 < args.size());
        if (opt == "--password" && has_value) {
            password = args[++i];
        } else if (opt == "--no-overwrite") {
            overwrite = false;
        } else if (opt == "--files" && has_value) {
            const std::string list = args[++i];
            size_t start = 0;
            while (start <= list.size()) {
                const size_t comma = list.find(',', start);
                const std::string item = list.substr(
                    start, comma == std::string::npos ? std::string::npos : comma - start);
                if (!item.empty()) only_files.push_back(item);
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
        } else {
            std::cerr << "未知参数: " << opt << "\n";
            return 1;
        }
    }

    ConsoleReporter reporter;
    dataguard::RestoreManager manager;
    const bool ok = manager.run(dataguard::utf8ToPath(args[1]), dataguard::utf8ToPath(args[2]),
                                password, overwrite, only_files, &reporter);
    if (!ok) {
        std::cout << "LOG\tERROR\t" << manager.last_error() << "\n";
        printDone(manager.last_success_count(), manager.last_failed_count(), "");
        return 1;
    }
    printDone(manager.last_success_count(), manager.last_failed_count(), args[1]);
    return 0;
}

int cmdList(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << "用法: dataguard-cli list <档案> [--password P]\n";
        return 1;
    }
    std::string password;
    for (size_t i = 2; i < args.size(); ++i) {
        if (args[i] == "--password" && i + 1 < args.size()) password = args[++i];
    }

    dataguard::ArchiveReader reader;
    if (!reader.open(dataguard::utf8ToPath(args[1]), password)) {
        std::cerr << reader.last_error() << "\n";
        return 2;
    }

    std::cout << "类型\t大小\t修改时间\t路径\n";
    for (const auto& entry : reader.entries()) {
        std::cout << kindName(entry.kind) << "\t" << entry.size << "\t"
                  << formatTime(entry.mtime) << "\t" << entry.rel_path_utf8 << "\n";
    }
    std::cout << "共 " << reader.entries().size() << " 个条目\n";
    return 0;
}

int cmdVerify(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << "用法: dataguard-cli verify <档案> [--password P]\n";
        return 1;
    }
    std::string password;
    for (size_t i = 2; i < args.size(); ++i) {
        if (args[i] == "--password" && i + 1 < args.size()) password = args[++i];
    }

    dataguard::ArchiveReader reader;
    if (!reader.open(dataguard::utf8ToPath(args[1]), password)) {
        std::cerr << reader.last_error() << "\n";
        return 2;
    }

    std::vector<std::string> bad_files;
    const bool ok = reader.verify(&bad_files);
    if (ok) {
        std::cout << "OK\t全部条目校验通过\n";
        return 0;
    }
    for (const auto& name : bad_files) {
        std::cout << "LOG\tERROR\t损坏条目: " << name << "\n";
    }
    std::cout << "FAIL\t" << bad_files.size() << " 个条目损坏\n";
    return 1;
}

}  // namespace

int main() {
    SetConsoleOutputCP(CP_UTF8);

    const std::vector<std::string> args = utf8Args();
    if (args.size() < 2) {
        printUsage();
        return 0;
    }

    const std::string& command = args[1];
    if (command == "--version" || command == "-v") {
        std::cout << kProductName << " " << kVersion << "\n";
        return 0;
    }
    if (command == "--help" || command == "-h") {
        printUsage();
        return 0;
    }

    // 子命令参数：sub[0] = 命令名，sub[1..] = 该命令的参数
    const std::vector<std::string> sub(args.begin() + 1, args.end());
    if (command == "backup") return cmdBackup(sub);
    if (command == "restore") return cmdRestore(sub);
    if (command == "list") return cmdList(sub);
    if (command == "verify") return cmdVerify(sub);

    std::cerr << "未知命令: " << command << "\n\n";
    printUsage();
    return 1;
}

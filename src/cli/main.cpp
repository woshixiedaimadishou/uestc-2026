#include <windows.h>

#include <cstring>
#include <iostream>
#include <string>

namespace {

constexpr const char* kProductName = "DataGuard";
constexpr const char* kVersion = "0.1.0";

void printUsage() {
    std::cout
        << kProductName << " " << kVersion << " - 数据备份软件（命令行入口）\n\n"
        << "用法:\n"
        << "  dataguard-cli --version        显示版本信息\n"
        << "  dataguard-cli --help           显示本帮助\n\n"
        << "说明: 当前为 v0.1（工程骨架与基础模块），备份/还原/压缩等命令将在\n"
        << "      后续版本（v0.2 起）逐步提供。\n";
}

}  // namespace

int main(int argc, char** argv) {
    // 控制台输出统一使用 UTF-8，避免中文乱码
    SetConsoleOutputCP(CP_UTF8);

    if (argc >= 2) {
        const char* arg = argv[1];
        if (std::strcmp(arg, "--version") == 0 || std::strcmp(arg, "-v") == 0) {
            std::cout << kProductName << " " << kVersion << "\n";
            return 0;
        }
        if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
            printUsage();
            return 0;
        }
        std::cerr << "未知参数: " << arg << "\n\n";
        printUsage();
        return 1;
    }

    printUsage();
    return 0;
}

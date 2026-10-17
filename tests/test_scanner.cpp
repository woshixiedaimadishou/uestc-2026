#include <gtest/gtest.h>
#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "dataguard/filter.h"
#include "dataguard/scanner.h"

namespace fs = std::filesystem;
using dataguard::FileEntry;
using dataguard::FileKind;
using dataguard::FileScanner;
using dataguard::Filter;
using dataguard::FilterSpec;

namespace {

class ScannerTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() /
                ("dg_scanner_" + std::to_string(GetCurrentProcessId()) + "_" +
                 std::to_string(GetTickCount64()));
        fs::create_directories(root_ / "sub");
        fs::create_directories(root_ / "empty");
        writeFile(root_ / "a.txt", "hello");
        writeFile(root_ / "sub" / "b.cpp", "int main() { return 0; }");
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }

    static void writeFile(const fs::path& path, const std::string& content) {
        std::ofstream out(path, std::ios::binary);
        out << content;
    }

    static std::vector<std::string> relPaths(const std::vector<FileEntry>& entries) {
        std::vector<std::string> rels;
        rels.reserve(entries.size());
        for (const auto& e : entries) rels.push_back(e.rel_path_utf8);
        return rels;
    }

    static size_t indexOf(const std::vector<FileEntry>& entries, const std::string& rel) {
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].rel_path_utf8 == rel) return i;
        }
        return entries.size();
    }

    fs::path root_;
};

}  // namespace

// 用例 1：扫描目录树得到全部条目（2 个目录 + 2 个文件）
TEST_F(ScannerTest, ScansDirectoriesAndFiles) {
    FilterSpec spec;
    Filter filter(spec);
    FileScanner scanner(filter);

    const auto entries = scanner.scan(root_);

    ASSERT_EQ(entries.size(), 4u);
    const auto rels = relPaths(entries);
    EXPECT_NE(std::find(rels.begin(), rels.end(), "a.txt"), rels.end());
    EXPECT_NE(std::find(rels.begin(), rels.end(), "empty"), rels.end());
    EXPECT_NE(std::find(rels.begin(), rels.end(), "sub"), rels.end());
    EXPECT_NE(std::find(rels.begin(), rels.end(), "sub/b.cpp"), rels.end());
}

// 用例 2：父目录先于其子项输出（还原时可先创建目录）
TEST_F(ScannerTest, ParentDirectoryBeforeChildren) {
    FilterSpec spec;
    Filter filter(spec);
    FileScanner scanner(filter);

    const auto entries = scanner.scan(root_);
    EXPECT_LT(indexOf(entries, "sub"), indexOf(entries, "sub/b.cpp"));
}

// 用例 3：空目录被保留
TEST_F(ScannerTest, EmptyDirectoryKept) {
    FilterSpec spec;
    Filter filter(spec);
    FileScanner scanner(filter);

    const auto entries = scanner.scan(root_);
    const size_t idx = indexOf(entries, "empty");
    ASSERT_LT(idx, entries.size());
    EXPECT_EQ(entries[idx].kind, FileKind::Directory);
}

// 用例 4：目录条目的类型与文件大小
TEST_F(ScannerTest, EntryKindAndSize) {
    FilterSpec spec;
    Filter filter(spec);
    FileScanner scanner(filter);

    const auto entries = scanner.scan(root_);
    const size_t file_idx = indexOf(entries, "a.txt");
    ASSERT_LT(file_idx, entries.size());
    EXPECT_EQ(entries[file_idx].kind, FileKind::File);
    EXPECT_EQ(entries[file_idx].size, 5u);   // "hello"

    const size_t dir_idx = indexOf(entries, "sub");
    ASSERT_LT(dir_idx, entries.size());
    EXPECT_EQ(entries[dir_idx].kind, FileKind::Directory);
}

// 用例 5：筛选规则作用于文件（目录始终保留）
TEST_F(ScannerTest, FilterAppliesToFilesOnly) {
    FilterSpec spec;
    spec.include_exts = {".cpp"};
    Filter filter(spec);
    FileScanner scanner(filter);

    const auto entries = scanner.scan(root_);
    const auto rels = relPaths(entries);

    EXPECT_EQ(std::find(rels.begin(), rels.end(), "a.txt"), rels.end());
    EXPECT_NE(std::find(rels.begin(), rels.end(), "sub/b.cpp"), rels.end());
    // 目录仍保留
    EXPECT_NE(std::find(rels.begin(), rels.end(), "empty"), rels.end());
    EXPECT_NE(std::find(rels.begin(), rels.end(), "sub"), rels.end());
}

// 用例 6：源路径不存在时通过 on_error 报告且不崩溃
TEST_F(ScannerTest, MissingRootReportsError) {
    FilterSpec spec;
    Filter filter(spec);
    FileScanner scanner(filter);

    std::vector<std::string> errors;
    const auto entries = scanner.scan(
        root_ / "not_exists", {},
        [&errors](const std::string& msg) { errors.push_back(msg); });

    EXPECT_TRUE(entries.empty());
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_FALSE(errors[0].empty());
}

// 用例 7：on_entry 回调与最终结果条数一致
TEST_F(ScannerTest, OnEntryCallbackCount) {
    FilterSpec spec;
    Filter filter(spec);
    FileScanner scanner(filter);

    size_t callback_count = 0;
    const auto entries = scanner.scan(root_, [&callback_count](const FileEntry&) {
        ++callback_count;
    });

    EXPECT_EQ(callback_count, entries.size());
}

// 用例 8：符号链接默认不跟随（作为链接条目记录）
TEST_F(ScannerTest, SymlinkNotFollowed) {
    const fs::path link_path = root_ / "link_to_sub";
    std::error_code ec;
    fs::create_directory_symlink(root_ / "sub", link_path, ec);
    if (ec) {
        GTEST_SKIP() << "当前环境无法创建符号链接（需要开发者模式或管理员权限）";
    }

    FilterSpec spec;
    Filter filter(spec);
    FileScanner scanner(filter);
    const auto entries = scanner.scan(root_);

    const size_t link_idx = indexOf(entries, "link_to_sub");
    ASSERT_LT(link_idx, entries.size());
    EXPECT_EQ(entries[link_idx].kind, FileKind::Symlink);
    // 链接目标下的文件不应被递归扫描进来
    EXPECT_EQ(indexOf(entries, "link_to_sub/b.cpp"), entries.size());
}

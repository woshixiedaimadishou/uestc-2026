#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "dataguard/filter.h"

using dataguard::FileEntry;
using dataguard::FileKind;
using dataguard::Filter;
using dataguard::FilterSpec;

namespace {

FileEntry makeFile(const std::string& rel,
                   uint64_t size = 100,
                   int64_t mtime = 1000000000,
                   const std::string& owner = "alice") {
    FileEntry entry;
    entry.rel_path_utf8 = rel;
    entry.kind = FileKind::File;
    entry.size = size;
    entry.mtime = mtime;
    entry.owner = owner;
    return entry;
}

FileEntry makeDir(const std::string& rel) {
    FileEntry entry;
    entry.rel_path_utf8 = rel;
    entry.kind = FileKind::Directory;
    return entry;
}

}  // namespace

// 用例 1：目录恒通过（目录不作为筛选对象）
TEST(FilterTest, DirectoryAlwaysMatches) {
    FilterSpec spec;
    spec.include_exts = {".cpp"};
    Filter filter(spec);
    EXPECT_TRUE(filter.matches(makeDir("src")));
}

// 用例 2：扩展名包含规则
TEST(FilterTest, IncludeExtension) {
    FilterSpec spec;
    spec.include_exts = {".cpp", ".h"};
    Filter filter(spec);
    EXPECT_TRUE(filter.matches(makeFile("src/main.cpp")));
    EXPECT_TRUE(filter.matches(makeFile("include/a.h")));
    EXPECT_FALSE(filter.matches(makeFile("docs/readme.md")));
}

// 用例 3：exclude 优先于 include
TEST(FilterTest, ExcludeHasPriority) {
    FilterSpec spec;
    spec.include_exts = {".cpp"};
    spec.exclude_exts = {".cpp"};
    Filter filter(spec);
    EXPECT_FALSE(filter.matches(makeFile("a.cpp")));
}

// 用例 4：路径 glob（* 匹配任意长度）
TEST(FilterTest, PathGlob) {
    FilterSpec spec;
    spec.include_path_globs = {"src/*"};
    Filter filter(spec);
    EXPECT_TRUE(filter.matches(makeFile("src/a.cpp")));
    EXPECT_TRUE(filter.matches(makeFile("src/sub/b.cpp")));
    EXPECT_FALSE(filter.matches(makeFile("docs/a.md")));
}

// 用例 5：路径排除规则
TEST(FilterTest, ExcludePathGlob) {
    FilterSpec spec;
    spec.exclude_path_globs = {"*/build/*"};
    Filter filter(spec);
    EXPECT_FALSE(filter.matches(makeFile("proj/build/out.o")));
    EXPECT_TRUE(filter.matches(makeFile("proj/src/main.cpp")));
}

// 用例 6：文件名 glob（只匹配文件名部分）
TEST(FilterTest, NameGlob) {
    FilterSpec spec;
    spec.include_name_globs = {"test_*"};
    Filter filter(spec);
    EXPECT_TRUE(filter.matches(makeFile("tests/test_archive.cpp")));
    EXPECT_FALSE(filter.matches(makeFile("tests/main.cpp")));
}

// 用例 7：扩展名比较大小写不敏感
TEST(FilterTest, CaseInsensitiveExtension) {
    FilterSpec spec;
    spec.include_exts = {".CPP"};
    Filter filter(spec);
    EXPECT_TRUE(filter.matches(makeFile("a.cpp")));
    EXPECT_TRUE(filter.matches(makeFile("b.CPP")));
}

// 用例 8：尺寸范围
TEST(FilterTest, SizeRange) {
    FilterSpec spec;
    spec.min_size = 100;
    spec.max_size = 1000;
    Filter filter(spec);
    EXPECT_FALSE(filter.matches(makeFile("small.bin", 99)));
    EXPECT_TRUE(filter.matches(makeFile("ok.bin", 100)));
    EXPECT_TRUE(filter.matches(makeFile("ok2.bin", 1000)));
    EXPECT_FALSE(filter.matches(makeFile("big.bin", 1001)));
}

// 用例 9：修改时间范围
TEST(FilterTest, TimeRange) {
    FilterSpec spec;
    spec.modified_after = 1000;
    spec.modified_before = 2000;
    Filter filter(spec);
    EXPECT_FALSE(filter.matches(makeFile("old.txt", 100, 999)));
    EXPECT_TRUE(filter.matches(makeFile("in.txt", 100, 1500)));
    EXPECT_FALSE(filter.matches(makeFile("new.txt", 100, 2001)));
}

// 用例 10：属主筛选（不区分大小写）+ 排除优先
TEST(FilterTest, OwnerFilter) {
    FilterSpec spec;
    spec.include_owners = {"Alice"};
    Filter filter(spec);
    EXPECT_TRUE(filter.matches(makeFile("a.txt", 100, 1000, "alice")));
    EXPECT_FALSE(filter.matches(makeFile("b.txt", 100, 1000, "bob")));

    FilterSpec exclude_spec;
    exclude_spec.exclude_owners = {"alice"};
    Filter exclude_filter(exclude_spec);
    EXPECT_FALSE(exclude_filter.matches(makeFile("c.txt", 100, 1000, "ALICE")));
}

// 用例 11：多规则组合时必须同时满足
TEST(FilterTest, CombinedRules) {
    FilterSpec spec;
    spec.include_exts = {".cpp"};
    spec.min_size = 10;
    spec.include_owners = {"alice"};
    Filter filter(spec);
    EXPECT_TRUE(filter.matches(makeFile("a.cpp", 20, 1000, "alice")));
    EXPECT_FALSE(filter.matches(makeFile("a.cpp", 5, 1000, "alice")));
    EXPECT_FALSE(filter.matches(makeFile("a.cpp", 20, 1000, "bob")));
    EXPECT_FALSE(filter.matches(makeFile("a.txt", 20, 1000, "alice")));
}

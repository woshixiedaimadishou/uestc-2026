#include <gtest/gtest.h>
#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "dataguard/archive_format.h"
#include "dataguard/archive_reader.h"
#include "dataguard/archive_writer.h"
#include "dataguard/filter.h"
#include "dataguard/scanner.h"

namespace fs = std::filesystem;
using dataguard::ArchiveOptions;
using dataguard::ArchiveReader;
using dataguard::ArchiveWriter;
using dataguard::FileEntry;
using dataguard::FileKind;
using dataguard::FileScanner;
using dataguard::Filter;
using dataguard::FilterSpec;

namespace {

class ArchiveTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() /
                ("dg_archive_" + std::to_string(GetCurrentProcessId()) + "_" +
                 std::to_string(GetTickCount64()));
        source_ = root_ / "src";
        fs::create_directories(source_ / "sub");
        fs::create_directories(source_ / "empty");
        writeFile(source_ / "a.txt", "hello world");
        writeFile(source_ / "sub" / "b.cpp", "int main(){return 0;}");
        archive_ = root_ / "test.bkpk";
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }

    static void writeFile(const fs::path& path, const std::string& content) {
        std::ofstream out(path, std::ios::binary);
        out << content;
    }

    static std::string readFile(const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in),
                           std::istreambuf_iterator<char>());
    }

    bool buildArchive() {
        FilterSpec spec;
        Filter filter(spec);
        FileScanner scanner(filter);
        const auto entries = scanner.scan(source_);

        ArchiveWriter writer;
        if (!writer.open(archive_, ArchiveOptions{})) return false;
        for (const auto& entry : entries) {
            if (!writer.add(entry)) return false;
        }
        return writer.close();
    }

    static const FileEntry* findEntry(const std::vector<FileEntry>& entries,
                                      const std::string& rel) {
        for (const auto& entry : entries) {
            if (entry.rel_path_utf8 == rel) return &entry;
        }
        return nullptr;
    }

    fs::path root_;
    fs::path source_;
    fs::path archive_;
};

}  // namespace

// 用例 1：档案写读回环 —— 条目数量、类型、大小正确
TEST_F(ArchiveTest, RoundTripEntries) {
    ASSERT_TRUE(buildArchive());

    ArchiveReader reader;
    ASSERT_TRUE(reader.open(archive_)) << reader.last_error();
    ASSERT_EQ(reader.entries().size(), 4u);

    const FileEntry* a_txt = findEntry(reader.entries(), "a.txt");
    ASSERT_NE(a_txt, nullptr);
    EXPECT_EQ(a_txt->kind, FileKind::File);
    EXPECT_EQ(a_txt->size, 11u);
    EXPECT_NE(a_txt->checksum, 0u);

    const FileEntry* empty_dir = findEntry(reader.entries(), "empty");
    ASSERT_NE(empty_dir, nullptr);
    EXPECT_EQ(empty_dir->kind, FileKind::Directory);
    EXPECT_EQ(empty_dir->size, 0u);
}

// 用例 2：还原后文件内容与源一致
TEST_F(ArchiveTest, RestoreContentsMatch) {
    ASSERT_TRUE(buildArchive());

    const fs::path restored = root_ / "restored";
    ArchiveReader reader;
    ASSERT_TRUE(reader.open(archive_)) << reader.last_error();
    ASSERT_TRUE(reader.extractAll(restored)) << reader.last_error();

    EXPECT_EQ(readFile(restored / "a.txt"), "hello world");
    EXPECT_EQ(readFile(restored / "sub" / "b.cpp"), "int main(){return 0;}");
}

// 用例 3：空目录在还原后保留
TEST_F(ArchiveTest, EmptyDirectoryPreserved) {
    ASSERT_TRUE(buildArchive());

    const fs::path restored = root_ / "restored";
    ArchiveReader reader;
    ASSERT_TRUE(reader.open(archive_));
    ASSERT_TRUE(reader.extractAll(restored));

    EXPECT_TRUE(fs::is_directory(restored / "empty"));
}

// 用例 4：verify 对完好档案返回 true
TEST_F(ArchiveTest, VerifyIntactArchive) {
    ASSERT_TRUE(buildArchive());

    ArchiveReader reader;
    ASSERT_TRUE(reader.open(archive_));
    std::vector<std::string> bad_files;
    EXPECT_TRUE(reader.verify(&bad_files));
    EXPECT_TRUE(bad_files.empty());
}

// 用例 5：篡改数据区 1 字节 → verify 检出损坏
TEST_F(ArchiveTest, VerifyDetectsTamperedData) {
    ASSERT_TRUE(buildArchive());

    std::vector<uint8_t> bytes;
    {
        std::ifstream in(archive_, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    ASSERT_GT(bytes.size(), 40u);
    bytes[bytes.size() - 1] ^= 0xFF;  // 破坏数据区最后一个字节
    {
        std::ofstream out(archive_, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }

    ArchiveReader reader;
    ASSERT_TRUE(reader.open(archive_)) << reader.last_error();
    std::vector<std::string> bad_files;
    EXPECT_FALSE(reader.verify(&bad_files));
    EXPECT_FALSE(bad_files.empty());
}

// 用例 6：魔数损坏 → open 失败
TEST_F(ArchiveTest, BadMagicRejected) {
    ASSERT_TRUE(buildArchive());

    std::vector<uint8_t> bytes;
    {
        std::ifstream in(archive_, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    ASSERT_GT(bytes.size(), 4u);
    bytes[0] ^= 0xFF;
    {
        std::ofstream out(archive_, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }

    ArchiveReader reader;
    EXPECT_FALSE(reader.open(archive_));
    EXPECT_FALSE(reader.last_error().empty());
}

// 用例 7：条目表损坏（table_crc 不符）→ open 失败
TEST_F(ArchiveTest, TamperedEntryTableRejected) {
    ASSERT_TRUE(buildArchive());

    std::vector<uint8_t> bytes;
    {
        std::ifstream in(archive_, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    // table_offset 位于固定头 [14..18)
    const uint32_t table_offset = dataguard::readU32(bytes.data() + 14);
    ASSERT_LT(table_offset + 1, bytes.size());
    bytes[table_offset] ^= 0xFF;  // 破坏条目表首字节
    {
        std::ofstream out(archive_, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }

    ArchiveReader reader;
    EXPECT_FALSE(reader.open(archive_));
}

// 用例 8：空档案（仅目录结构之外的 0 条目）也能正常读写
TEST_F(ArchiveTest, EmptySourceArchive) {
    const fs::path empty_source = root_ / "empty_src";
    fs::create_directories(empty_source);

    FilterSpec spec;
    Filter filter(spec);
    FileScanner scanner(filter);
    const auto entries = scanner.scan(empty_source);
    EXPECT_TRUE(entries.empty());

    ArchiveWriter writer;
    ASSERT_TRUE(writer.open(archive_, ArchiveOptions{}));
    ASSERT_TRUE(writer.close());

    ArchiveReader reader;
    ASSERT_TRUE(reader.open(archive_)) << reader.last_error();
    EXPECT_TRUE(reader.entries().empty());
    EXPECT_TRUE(reader.verify());
}

// 用例 9：v0.3 已支持压缩；加密仍需 v0.4
TEST_F(ArchiveTest, UnsupportedOptionsRejected) {
    ArchiveWriter writer;

    // 加密尚未实现 → 应明确拒绝并提示 v0.4
    ArchiveOptions encrypt_options;
    encrypt_options.encrypt = true;
    encrypt_options.password = "secret";
    EXPECT_FALSE(writer.open(archive_, encrypt_options));
    EXPECT_NE(writer.last_error().find("v0.4"), std::string::npos);
}

// 用例 11：压缩档案写读回环（compressor_id=1，内容正确还原）
TEST_F(ArchiveTest, CompressedArchiveRoundTrip) {
    FilterSpec spec;
    Filter filter(spec);
    FileScanner scanner(filter);
    const auto entries = scanner.scan(source_);

    ArchiveOptions options;
    options.compress = true;

    ArchiveWriter writer;
    ASSERT_TRUE(writer.open(archive_, options)) << writer.last_error();
    for (const auto& entry : entries) {
        ASSERT_TRUE(writer.add(entry)) << writer.last_error();
    }
    ASSERT_TRUE(writer.close()) << writer.last_error();

    // 档案头 compressor_id（偏移 8）= 1 表示 DEFLATE
    std::vector<uint8_t> header;
    {
        std::ifstream in(archive_, std::ios::binary);
        header.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    ASSERT_GT(header.size(), 38u);
    EXPECT_EQ(header[8], dataguard::kCompressorDeflate);

    const fs::path restored = root_ / "restored_compressed";
    ArchiveReader reader;
    ASSERT_TRUE(reader.open(archive_)) << reader.last_error();
    EXPECT_TRUE(reader.compressed());
    EXPECT_TRUE(reader.verify());
    ASSERT_TRUE(reader.extractAll(restored));

    EXPECT_EQ(readFile(restored / "a.txt"), "hello world");
    EXPECT_EQ(readFile(restored / "sub" / "b.cpp"), "int main(){return 0;}");
}

// 用例 10：打开不存在的档案失败且给出错误信息
TEST_F(ArchiveTest, OpenMissingArchiveFails) {
    ArchiveReader reader;
    EXPECT_FALSE(reader.open(root_ / "not_exists.bkpk"));
    EXPECT_FALSE(reader.last_error().empty());
}

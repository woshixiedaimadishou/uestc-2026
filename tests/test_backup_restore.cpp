#include <gtest/gtest.h>
#include <windows.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "dataguard/archive_reader.h"
#include "dataguard/archive_writer.h"
#include "dataguard/backup_manager.h"
#include "dataguard/retention.h"

namespace fs = std::filesystem;
using dataguard::ArchiveOptions;
using dataguard::ArchiveReader;
using dataguard::BackupManager;
using dataguard::BackupTaskConfig;
using dataguard::FileKind;
using dataguard::RestoreManager;
using dataguard::RetentionOptions;

namespace {

class BackupRestoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() /
                ("dg_br_" + std::to_string(GetCurrentProcessId()) + "_" +
                 std::to_string(GetTickCount64()));
        source_ = root_ / "src";
        fs::create_directories(source_ / "sub");
        fs::create_directories(source_ / "empty");
        writeFile(source_ / "a.txt", "hello world");
        writeFile(source_ / "sub" / "b.bin", std::string(1000, '\x7F'));
        writeFile(source_ / u8"\u4e2d\u6587\u6587\u4ef6.txt", "chinese content");
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

    bool runBackup(const ArchiveOptions& options, fs::path& archive) {
        BackupTaskConfig config;
        config.name = "test";
        config.source = source_;
        config.dest_dir = root_ / "out";
        config.options = options;

        BackupManager manager;
        if (!manager.run(config, nullptr)) return false;
        archive = manager.last_archive();
        return true;
    }

    bool runRestore(const fs::path& archive, const fs::path& dest, const std::string& password) {
        RestoreManager manager;
        return manager.run(archive, dest, password, true, {}, nullptr);
    }

    fs::path root_;
    fs::path source_;
};

}  // namespace

// 用例 1：无压缩无加密端到端
TEST_F(BackupRestoreTest, EndToEndPlain) {
    ArchiveOptions options;
    fs::path archive;
    ASSERT_TRUE(runBackup(options, archive));

    const fs::path restored = root_ / "restored_plain";
    ASSERT_TRUE(runRestore(archive, restored, ""));

    EXPECT_EQ(readFile(restored / "a.txt"), "hello world");
    EXPECT_EQ(readFile(restored / "sub" / "b.bin"), std::string(1000, '\x7F'));
    EXPECT_TRUE(fs::is_directory(restored / "empty"));
    EXPECT_EQ(readFile(restored / u8"\u4e2d\u6587\u6587\u4ef6.txt"), "chinese content");
}

// 用例 2：压缩档案端到端
TEST_F(BackupRestoreTest, EndToEndCompressed) {
    ArchiveOptions options;
    options.compress = true;

    fs::path archive;
    ASSERT_TRUE(runBackup(options, archive));

    const fs::path restored = root_ / "restored_zip";
    ASSERT_TRUE(runRestore(archive, restored, ""));
    EXPECT_EQ(readFile(restored / "a.txt"), "hello world");
    EXPECT_EQ(readFile(restored / "sub" / "b.bin"), std::string(1000, '\x7F'));
}

// 用例 3：加密档案端到端（正确密码）
TEST_F(BackupRestoreTest, EndToEndEncrypted) {
    ArchiveOptions options;
    options.encrypt = true;
    options.password = "MySecret123";

    fs::path archive;
    ASSERT_TRUE(runBackup(options, archive));

    const fs::path restored = root_ / "restored_enc";
    ASSERT_TRUE(runRestore(archive, restored, "MySecret123"));
    EXPECT_EQ(readFile(restored / "a.txt"), "hello world");
}

// 用例 4：错误密码被拒绝
TEST_F(BackupRestoreTest, WrongPasswordRejected) {
    ArchiveOptions options;
    options.encrypt = true;
    options.password = "correct";

    fs::path archive;
    ASSERT_TRUE(runBackup(options, archive));

    RestoreManager manager;
    EXPECT_FALSE(manager.run(archive, root_ / "restored_bad", "wrong", true, {}, nullptr));
    EXPECT_NE(manager.last_error().find("密码"), std::string::npos);
}

// 用例 5：压缩 + 加密组合
TEST_F(BackupRestoreTest, EndToEndCompressedAndEncrypted) {
    ArchiveOptions options;
    options.compress = true;
    options.encrypt = true;
    options.password = "combo-pass";

    fs::path archive;
    ASSERT_TRUE(runBackup(options, archive));

    const fs::path restored = root_ / "restored_combo";
    ASSERT_TRUE(runRestore(archive, restored, "combo-pass"));
    EXPECT_EQ(readFile(restored / "a.txt"), "hello world");
    EXPECT_EQ(readFile(restored / "sub" / "b.bin"), std::string(1000, '\x7F'));
}

// 用例 6：元数据（修改时间）在还原后保持一致
TEST_F(BackupRestoreTest, MetadataPreserved) {
    ArchiveOptions options;
    fs::path archive;
    ASSERT_TRUE(runBackup(options, archive));

    const fs::path restored = root_ / "restored_meta";
    ASSERT_TRUE(runRestore(archive, restored, ""));

    std::error_code ec_src;
    std::error_code ec_dst;
    const auto src_time = fs::last_write_time(source_ / "a.txt", ec_src);
    const auto dst_time = fs::last_write_time(restored / "a.txt", ec_dst);
    ASSERT_FALSE(ec_src);
    ASSERT_FALSE(ec_dst);

    // 允许 2 秒误差（FAT/时间精度差异）
    const auto diff = std::chrono::duration_cast<std::chrono::seconds>(src_time - dst_time).count();
    EXPECT_LE(diff < 0 ? -diff : diff, 2);
}

// 用例 7：硬链接去重 —— 档案中只存一份数据，还原后仍是硬链接
TEST_F(BackupRestoreTest, HardLinkDeduplicated) {
    const fs::path original = source_ / "hardlink_a.txt";
    const fs::path linked = source_ / "hardlink_b.txt";
    writeFile(original, "hardlink shared content");

    std::error_code ec;
    fs::create_hard_link(original, linked, ec);
    if (ec) {
        GTEST_SKIP() << "当前环境无法创建硬链接: " << ec.message();
    }

    ArchiveOptions options;
    fs::path archive;
    ASSERT_TRUE(runBackup(options, archive));

    ArchiveReader reader;
    ASSERT_TRUE(reader.open(archive)) << reader.last_error();

    int hardlink_entries = 0;
    for (const auto& entry : reader.entries()) {
        if (entry.kind == FileKind::Hardlink) {
            ++hardlink_entries;
            EXPECT_EQ(entry.stored_size, 0u);
            EXPECT_FALSE(entry.link_target_utf8.empty());
        }
    }
    EXPECT_EQ(hardlink_entries, 1);

    const fs::path restored = root_ / "restored_hardlink";
    ASSERT_TRUE(runRestore(archive, restored, ""));
    EXPECT_EQ(readFile(restored / "hardlink_a.txt"), "hardlink shared content");
    EXPECT_EQ(readFile(restored / "hardlink_b.txt"), "hardlink shared content");

    std::error_code count_ec;
    EXPECT_EQ(fs::hard_link_count(restored / "hardlink_b.txt", count_ec), 2u);
}

// 用例 8：数据淘汰 —— 保留最近 N 份
TEST_F(BackupRestoreTest, RetentionKeepsLatestN) {
    const fs::path dest = root_ / "retention_out";
    fs::create_directories(dest);

    const auto base = std::chrono::system_clock::now();
    for (int i = 0; i < 5; ++i) {
        const fs::path file = dest / ("task_2026102" + std::to_string(i) + "_000000.bkpk");
        writeFile(file, "archive");
        // 设置递增的修改时间（i 越大越新）
        std::error_code ec;
        fs::last_write_time(file, fs::file_time_type::clock::now() +
                                      std::chrono::hours(24 * i), ec);
    }

    RetentionOptions options;
    options.keep_count = 2;

    std::vector<std::string> logs;
    const auto result = dataguard::applyRetention(dest, "task", options, &logs);

    EXPECT_EQ(result.removed.size(), 3u);
    EXPECT_EQ(result.kept, 2u);
    EXPECT_EQ(result.failed, 0u);

    // 剩余 2 个应为最新的两个（task_20261023、task_20261024）
    int remaining = 0;
    for (const auto& item : fs::directory_iterator(dest)) {
        if (item.is_regular_file()) ++remaining;
    }
    EXPECT_EQ(remaining, 2);
}

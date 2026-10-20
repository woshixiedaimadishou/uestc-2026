"""命令行参数组装测试。"""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "python"))

from cli_client import (  # noqa: E402
    CMD_LINE_LIMIT,
    build_backup_args,
    build_list_args,
    build_restore_args,
    build_retention_args,
    write_selector_file,
)

CLI = r"C:\app\dataguard-cli.exe"


class BuildBackupArgsTest(unittest.TestCase):
    def test_minimal(self):
        args = build_backup_args(CLI, "src", "dst")
        self.assertEqual(args[0], CLI)
        self.assertEqual(args[1], "backup")
        self.assertEqual(args[2], "src")
        self.assertEqual(args[3], "dst")
        self.assertIn("--name", args)

    def test_compress_and_encrypt(self):
        args = build_backup_args(CLI, "src", "dst", name="job", compress=True,
                                 encrypt=True, password="secret")
        self.assertIn("--compress", args)
        self.assertIn("--encrypt", args)
        self.assertEqual(args[args.index("--password") + 1], "secret")
        self.assertEqual(args[args.index("--name") + 1], "job")

    def test_filters_are_expanded(self):
        filters = {
            "include_exts": [".cpp", ".h"],
            "exclude_exts": [".tmp"],
            "include_path_globs": ["src/*"],
            "exclude_path_globs": ["*/build/*"],
            "include_owners": ["alice"],
            "min_size": 1024,
            "max_size": 1024 * 1024,
            "modified_after": "2026-10-01",
            "follow_symlinks": True,
        }
        args = build_backup_args(CLI, "src", "dst", filters=filters)
        self.assertEqual(args.count("--include-ext"), 2)
        self.assertIn("--exclude-ext", args)
        self.assertIn("--include-glob", args)
        self.assertIn("--exclude-glob", args)
        self.assertIn("--owner", args)
        self.assertEqual(args[args.index("--min-size") + 1], "1024")
        self.assertEqual(args[args.index("--max-size") + 1], "1048576")
        self.assertEqual(args[args.index("--modified-after") + 1], "2026-10-01")
        self.assertIn("--follow-symlinks", args)

    def test_negative_size_not_passed(self):
        args = build_backup_args(CLI, "src", "dst",
                                 filters={"min_size": -1, "max_size": -1})
        self.assertNotIn("--min-size", args)
        self.assertNotIn("--max-size", args)

    def test_empty_name_falls_back(self):
        args = build_backup_args(CLI, "src", "dst", name="")
        self.assertEqual(args[args.index("--name") + 1], "backup")


class BuildOtherArgsTest(unittest.TestCase):
    def test_restore_with_password(self):
        args = build_restore_args(CLI, "a.bkpk", "out", password="p", overwrite=True)
        self.assertEqual(args[1], "restore")
        self.assertIn("--password", args)
        self.assertNotIn("--no-overwrite", args)

    def test_restore_no_overwrite_and_files(self):
        args = build_restore_args(CLI, "a.bkpk", "out", overwrite=False,
                                  files=["a.txt", "sub/b.txt"])
        self.assertIn("--no-overwrite", args)
        self.assertEqual(args[args.index("--files") + 1], "a.txt,sub/b.txt")

    def test_restore_prefers_selector_file(self):
        """清单文件优先于逗号列表：避免上千条目拼成超长命令行。"""
        args = build_restore_args(CLI, "a.bkpk", "out",
                                  files=["a.txt"], files_from="C:/tmp/list.txt")
        self.assertIn("--files-from", args)
        self.assertEqual(args[args.index("--files-from") + 1], "C:/tmp/list.txt")
        self.assertNotIn("--files", args)

    def test_restore_without_selection_passes_no_filter(self):
        args = build_restore_args(CLI, "a.bkpk", "out")
        self.assertNotIn("--files", args)
        self.assertNotIn("--files-from", args)

    def test_retention_options(self):
        args = build_retention_args(CLI, "dst", "job", keep_count=3, keep_days=7)
        self.assertEqual(args[1], "retention")
        self.assertEqual(args[args.index("--keep-count") + 1], "3")
        self.assertEqual(args[args.index("--keep-days") + 1], "7")

    def test_retention_without_options(self):
        args = build_retention_args(CLI, "dst", "job")
        self.assertNotIn("--keep-count", args)
        self.assertNotIn("--keep-days", args)

    def test_list_args(self):
        args = build_list_args(CLI, "a.bkpk", password="p")
        self.assertEqual(args[1], "list")
        self.assertIn("--password", args)


class SelectorFileTest(unittest.TestCase):
    """回归：条目很多时命令行会超过 32767 字符（WinError 206）。"""

    def test_write_selector_file_roundtrip(self):
        paths = ["a.txt", "目录/b.txt", "c d/e.txt"]
        target = write_selector_file(paths)
        try:
            text = target.read_text(encoding="utf-8")
            self.assertEqual(text.splitlines(), paths)
            self.assertTrue(text.endswith("\n"))
        finally:
            target.unlink(missing_ok=True)

    def test_selector_file_keeps_command_line_short(self):
        paths = [f"bucket_{i // 100:02d}/nested/document_{i:05d}_payload.txt" for i in range(3000)]
        with tempfile.TemporaryDirectory() as tmp:
            target = write_selector_file(paths, directory=tmp)
            args = build_restore_args(CLI, "a.bkpk", "out", files_from=target)
            old_args = build_restore_args(CLI, "a.bkpk", "out", files=paths)
            new_len = len(subprocess.list2cmdline([str(a) for a in args]))
            old_len = len(subprocess.list2cmdline([str(a) for a in old_args]))
            self.assertLess(new_len, 1000)
            self.assertGreater(old_len, CMD_LINE_LIMIT)  # 旧方式必然超限
            self.assertEqual(len(target.read_text(encoding="utf-8").splitlines()), 3000)

    def test_selector_file_strips_line_breaks(self):
        target = write_selector_file(["a\r\nb.txt", "c.txt\n"])
        try:
            self.assertEqual(target.read_text(encoding="utf-8").splitlines(),
                             ["ab.txt", "c.txt"])
        finally:
            target.unlink(missing_ok=True)


if __name__ == "__main__":
    unittest.main()

"""CLI 行协议解析测试。"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "python"))

from cli_client import DONE, LOG, PROGRESS, parse_line  # noqa: E402


class ParseLineTest(unittest.TestCase):
    def test_progress_line(self):
        kind, payload = parse_line("PROGRESS\t3\t10\tsub/b.cpp\n")
        self.assertEqual(kind, PROGRESS)
        self.assertEqual(payload, (3, 10, "sub/b.cpp"))

    def test_progress_line_with_spaces_in_path(self):
        kind, payload = parse_line("PROGRESS\t1\t5\tdir with space/a.txt")
        self.assertEqual(kind, PROGRESS)
        self.assertEqual(payload[2], "dir with space/a.txt")

    def test_progress_b_chunk_line(self):
        kind, payload = parse_line("PROGRESS_B\t4\t65536\t1048576")
        self.assertEqual(kind, PROGRESS)
        self.assertEqual(payload[0], 4)

    def test_log_line(self):
        kind, payload = parse_line("LOG\tWARN\t部分文件失败")
        self.assertEqual(kind, LOG)
        self.assertEqual(payload, ("WARN", "部分文件失败"))

    def test_done_line(self):
        kind, payload = parse_line("DONE\t12\t1\tC:/out/backup_20261020_120000.bkpk")
        self.assertEqual(kind, DONE)
        self.assertEqual(payload[0], 12)
        self.assertEqual(payload[1], 1)
        self.assertTrue(payload[2].endswith(".bkpk"))

    def test_empty_and_unknown_lines(self):
        self.assertIsNone(parse_line(""))
        self.assertIsNone(parse_line("\n"))
        self.assertIsNone(parse_line("some random output"))

    def test_malformed_numbers_are_ignored(self):
        self.assertIsNone(parse_line("PROGRESS\tabc\tdef\tpath"))

    def test_ok_line_is_treated_as_log(self):
        kind, payload = parse_line("OK\t全部条目校验通过")
        self.assertEqual(kind, LOG)
        self.assertEqual(payload[0], "INFO")


if __name__ == "__main__":
    unittest.main()

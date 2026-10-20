"""数字归一化测试：区域设置导致的“中文数字”必须还原为 0-9。"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "python"))

from text_utils import to_ascii_digits  # noqa: E402


class ToAsciiDigitsTest(unittest.TestCase):
    def test_chinese_numeral_time(self):
        """zh_HK 区域下 QTimeEdit 会把 08:00 渲染成 〇〨:〇〇。"""
        self.assertEqual(to_ascii_digits("〇〨:〇〇"), "08:00")

    def test_chinese_numeral_date(self):
        self.assertEqual(to_ascii_digits("〢〇〢〦-〡〇-〢〇"), "2026-10-20")

    def test_all_hangzhou_digits(self):
        self.assertEqual(to_ascii_digits("〇〡〢〣〤〥〦〧〨〩"), "0123456789")

    def test_fullwidth_and_arabic_indic(self):
        self.assertEqual(to_ascii_digits("１２３"), "123")
        self.assertEqual(to_ascii_digits("٤٥٦"), "456")

    def test_plain_ascii_unchanged(self):
        text = "每天 08:00，保留最近 3 份"
        self.assertEqual(to_ascii_digits(text), text)

    def test_empty_input(self):
        self.assertEqual(to_ascii_digits(""), "")
        self.assertEqual(to_ascii_digits(None), None)

    def test_normalized_time_is_parsable(self):
        from datetime import datetime

        value = to_ascii_digits("〢〇〢〦-〡〇-〢〇 〇〨:〇〇")
        parsed = datetime.strptime(value, "%Y-%m-%d %H:%M")
        self.assertEqual((parsed.year, parsed.month, parsed.day, parsed.hour), (2026, 10, 20, 8))


if __name__ == "__main__":
    unittest.main()

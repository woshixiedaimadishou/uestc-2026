"""纯文本工具（不依赖 Qt，便于单元测试与后台调度复用）。"""

from __future__ import annotations

# 各类“非 ASCII 数字”到阿拉伯数字的映射：
# U+3007 〇 / U+3021-3029 〡〢…〩（中文、杭州数字）、U+FF10-FF19 全角、
# U+0660-0669 阿拉伯-印度数字、U+06F0-06F9 扩展阿拉伯-印度数字、
# U+0966-096F 天城文数字。
_DIGIT_MAP = {"\u3007": "0"}
for _i in range(9):
    _DIGIT_MAP[chr(0x3021 + _i)] = str(_i + 1)
for _base in (0xFF10, 0x0660, 0x06F0, 0x0966):
    for _i in range(10):
        _DIGIT_MAP[chr(_base + _i)] = str(_i)


def to_ascii_digits(text: str) -> str:
    """把文本中的非 ASCII 数字统一转换为 0-9。

    某些区域设置（如 zh_HK）下 Qt 会用中文数字渲染日期/时间，
    0 显示为 〇、8 显示为 〨。写回配置或参与解析前必须归一化，
    否则会生成 ``〇〨:〇〇`` 这类无法解析的时间串。
    """
    if not text:
        return text
    return "".join(_DIGIT_MAP.get(ch, ch) for ch in text)

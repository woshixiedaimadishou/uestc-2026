"""PySide6 运行时引导。

职责：
1. 让可用的 PySide6 运行时目录优先进入 ``sys.path``（本机 anaconda 自带
   6.12 依赖系统 ICU 的较新版本，加载 QtCore 会失败，已验证 6.8 可用）；
2. 统一数字显示：部分区域（如 zh_HK）在 CLDR 中把 native digits 定义为
   中文数字（〇一二三…〩），Qt 的日期/时间控件会按区域渲染成
   ``〇〨:〇〇`` 这种形式。导入 Qt 后立即把默认区域切到使用阿拉伯数字的
   等价区域，并提供给控件显式设置的安全区域。

若独立目录不存在（换机器/已全局安装可用版本），第 1 步直接跳过，
不影响运行。
"""

from __future__ import annotations

import sys
from pathlib import Path

# 按优先级排列的 PySide6 运行时目录
_RUNTIME_CANDIDATES = [
    Path("E:/pyside68"),
    Path("E:/pyside6_runtime"),
]

# 中文环境下优先使用的等价区域（使用阿拉伯数字 0-9）
_LATIN_FALLBACK_ZH = ("zh_CN", "zh_Hans_CN", "en_US", "C")
_LATIN_FALLBACK_OTHER = ("en_US", "C")

_safe_locale = None


def ensure() -> None:
    """把可用的 PySide6 运行时目录插入 sys.path 最前面，并统一数字显示。"""
    for candidate in _RUNTIME_CANDIDATES:
        if (candidate / "PySide6").is_dir():
            path = str(candidate)
            if path not in sys.path:
                sys.path.insert(0, path)
            break
    _apply_latin_digits()


def _apply_latin_digits() -> None:
    """若当前区域用非 ASCII 数字渲染数值，则切换到等价区域。"""
    try:
        from PySide6.QtCore import QLocale
    except Exception:  # noqa: BLE001 - 运行时目录不可用时交由上层报错
        return

    current = QLocale()
    if _uses_latin_digits(current):
        global _safe_locale
        _safe_locale = current
        return

    candidates = (_LATIN_FALLBACK_ZH if current.language() == QLocale.Language.Chinese
                  else _LATIN_FALLBACK_OTHER)
    for name in candidates:
        candidate = QLocale(name)
        if candidate.name() and _uses_latin_digits(candidate):
            QLocale.setDefault(candidate)
            _safe_locale = candidate
            return

    fallback = QLocale.c()
    QLocale.setDefault(fallback)
    _safe_locale = fallback


def _uses_latin_digits(locale) -> bool:
    """判断区域是否用阿拉伯数字（用 ``toString`` 实测，避免依赖内部字段）。"""
    try:
        return locale.toString(8) == "8"
    except Exception:  # noqa: BLE001
        return True


def ui_locale():
    """返回一个保证数字为 0-9 的区域对象，供日期/时间控件显式设置。"""
    if _safe_locale is not None:
        return _safe_locale
    try:
        from PySide6.QtCore import QLocale
    except Exception:  # noqa: BLE001
        return None
    _apply_latin_digits()
    return _safe_locale if _safe_locale is not None else QLocale()

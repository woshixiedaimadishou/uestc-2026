"""PySide6 运行时引导。

背景：本机 anaconda 自带的 PySide6 6.12 依赖系统 ICU 的较新版本，
在本机加载 QtCore 会失败（DLL load failed）。已验证 PySide6 6.8
可正常使用，因此安装到独立目录并在导入 Qt 之前优先加入 sys.path。

若独立目录不存在（换机器/已全局安装可用版本），直接跳过，不影响运行。
"""

from __future__ import annotations

import sys
from pathlib import Path

# 按优先级排列的 PySide6 运行时目录
_RUNTIME_CANDIDATES = [
    Path("E:/pyside68"),
    Path("E:/pyside6_runtime"),
]


def ensure() -> None:
    """把可用的 PySide6 运行时目录插入 sys.path 最前面。"""
    for candidate in _RUNTIME_CANDIDATES:
        if (candidate / "PySide6").is_dir():
            path = str(candidate)
            if path not in sys.path:
                sys.path.insert(0, path)
            return

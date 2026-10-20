"""DataGuard 图形界面入口。

开发运行：python python/dataguard_gui.py
打包运行：dist/DataGuard/DataGuard.exe（PyInstaller onedir）
"""

from __future__ import annotations

import sys
from pathlib import Path

# 保证以脚本方式运行时能找到同目录模块（PyInstaller 环境下同样适用）
CURRENT_DIR = Path(__file__).resolve().parent
if str(CURRENT_DIR) not in sys.path:
    sys.path.insert(0, str(CURRENT_DIR))

from qt_bootstrap import ensure as _ensure_qt_runtime  # noqa: E402

_ensure_qt_runtime()

from PySide6.QtWidgets import QApplication  # noqa: E402  (需在 sys.path 调整之后)

from main_window import MainWindow  # noqa: E402


def main() -> int:
    app = QApplication(sys.argv)
    app.setApplicationName("DataGuard")
    app.setApplicationDisplayName("DataGuard 数据备份软件")

    window = MainWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())

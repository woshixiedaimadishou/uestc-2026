"""GUI 公共组件：后台 CLI 工作线程与进度对话框。"""

from __future__ import annotations

from qt_bootstrap import ensure as _ensure_qt_runtime

_ensure_qt_runtime()

from PySide6.QtCore import QThread, Signal
from PySide6.QtWidgets import (
    QDialog,
    QHBoxLayout,
    QLabel,
    QPlainTextEdit,
    QProgressBar,
    QPushButton,
    QVBoxLayout,
)

# 各类“非 ASCII 数字”到阿拉伯数字的映射（实现见 text_utils，此处重新导出，
# 方便界面模块统一从 ui_common 引用）。
from text_utils import to_ascii_digits

__all__ = ["CliWorker", "ProgressDialog", "to_ascii_digits"]


class CliWorker(QThread):
    """在后台线程中执行 CLI 调用，避免阻塞界面。"""

    progress = Signal(int, int, str)
    log = Signal(str, str)
    done = Signal(bool, str)

    def __init__(self, client, action: str, kwargs: dict, parent=None):
        super().__init__(parent)
        self.client = client
        self.action = action
        self.kwargs = kwargs

    def run(self) -> None:  # noqa: D401 - QThread 入口
        try:
            kwargs = dict(self.kwargs)
            kwargs["on_progress"] = self.progress.emit
            kwargs["on_log"] = self.log.emit

            if self.action == "backup":
                result = self.client.backup(**kwargs)
            elif self.action == "restore":
                result = self.client.restore(**kwargs)
            else:
                raise ValueError(f"未知操作: {self.action}")

            if result.ok:
                message = f"完成：成功 {result.success} 项，失败 {result.failed} 项"
            else:
                message = result.message or "执行失败"
            self.done.emit(result.ok, message)
        except Exception as exc:  # noqa: BLE001 - 线程边界需要兜底
            self.done.emit(False, str(exc))


class ProgressDialog(QDialog):
    """显示总进度、当前文件与日志。"""

    def __init__(self, title: str, subtitle: str = "", parent=None):
        super().__init__(parent)
        self.setWindowTitle(title)
        self.resize(560, 420)

        layout = QVBoxLayout(self)

        self.subtitle_label = QLabel(subtitle)
        layout.addWidget(self.subtitle_label)

        self.current_label = QLabel("准备中…")
        self.current_label.setWordWrap(True)
        layout.addWidget(self.current_label)

        self.progress_bar = QProgressBar()
        self.progress_bar.setRange(0, 100)
        self.progress_bar.setValue(0)
        layout.addWidget(self.progress_bar)

        self.log_view = QPlainTextEdit()
        self.log_view.setReadOnly(True)
        layout.addWidget(self.log_view, 1)

        button_row = QHBoxLayout()
        button_row.addStretch(1)
        self.close_button = QPushButton("关闭")
        self.close_button.setEnabled(False)
        self.close_button.clicked.connect(self.accept)
        button_row.addWidget(self.close_button)
        layout.addLayout(button_row)

    def on_progress(self, index: int, total: int, path: str) -> None:
        if total > 0:
            self.progress_bar.setMaximum(total)
            self.progress_bar.setValue(min(index, total))
        if path:
            self.current_label.setText(f"正在处理：{path}")
        elif index > 0:
            self.current_label.setText(f"已处理 {index}/{total}")

    def on_log(self, level: str, message: str) -> None:
        self.log_view.appendPlainText(f"[{level}] {message}")

    def finish(self, ok: bool, message: str) -> None:
        self.progress_bar.setValue(self.progress_bar.maximum())
        self.current_label.setText(message)
        self.log_view.appendPlainText(("完成：" if ok else "失败：") + message)
        self.close_button.setEnabled(True)
        self.close_button.setDefault(True)

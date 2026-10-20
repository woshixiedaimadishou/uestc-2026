"""还原对话框：选择档案 → 勾选条目 → 还原到目标目录。"""

from __future__ import annotations

from pathlib import Path

from qt_bootstrap import ensure as _ensure_qt_runtime

_ensure_qt_runtime()

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QCheckBox,
    QDialog,
    QFileDialog,
    QFormLayout,
    QHBoxLayout,
    QLineEdit,
    QMessageBox,
    QPushButton,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from cli_client import CliClient
from ui_common import CliWorker, ProgressDialog


def browse_directory(edit: QLineEdit, caption: str, parent: QWidget) -> QWidget:
    container = QWidget(parent)
    layout = QHBoxLayout(container)
    layout.setContentsMargins(0, 0, 0, 0)
    button = QPushButton("浏览…")
    button.setFixedWidth(80)

    def choose() -> None:
        start = edit.text().strip() or str(Path.home())
        path = QFileDialog.getExistingDirectory(parent, caption, start)
        if path:
            edit.setText(path)

    button.clicked.connect(choose)
    layout.addWidget(edit, 1)
    layout.addWidget(button)
    return container


class RestoreDialog(QDialog):
    def __init__(self, client: CliClient, parent=None):
        super().__init__(parent)
        self.client = client
        self.worker: CliWorker | None = None
        self.progress_dialog: ProgressDialog | None = None

        self.setWindowTitle("还原备份")
        self.resize(720, 560)

        self.archive_edit = QLineEdit()
        archive_row = QWidget()
        archive_layout = QHBoxLayout(archive_row)
        archive_layout.setContentsMargins(0, 0, 0, 0)
        browse_button = QPushButton("选择档案…")
        browse_button.setFixedWidth(100)
        browse_button.clicked.connect(self.choose_archive)
        archive_layout.addWidget(self.archive_edit, 1)
        archive_layout.addWidget(browse_button)

        self.password_edit = QLineEdit()
        self.password_edit.setEchoMode(QLineEdit.Password)
        self.password_edit.setPlaceholderText("档案未加密可留空")

        self.dest_edit = QLineEdit()

        form = QFormLayout()
        form.addRow("档案", archive_row)
        form.addRow("口令", self.password_edit)
        form.addRow("目标目录", browse_directory(self.dest_edit, "选择还原目标目录", self))

        self.tree = QTreeWidget()
        self.tree.setColumnCount(2)
        self.tree.setHeaderLabels(["条目", "大小（字节）"])
        self.tree.setColumnWidth(0, 480)

        self.overwrite_check = QCheckBox("覆盖已存在的文件")
        self.overwrite_check.setChecked(True)

        action_row = QHBoxLayout()
        self.load_button = QPushButton("加载清单")
        self.load_button.clicked.connect(self.load_entries)
        self.select_all_button = QPushButton("全选")
        self.select_all_button.clicked.connect(lambda: self.set_all_checked(True))
        self.select_none_button = QPushButton("全不选")
        self.select_none_button.clicked.connect(lambda: self.set_all_checked(False))
        action_row.addWidget(self.load_button)
        action_row.addWidget(self.select_all_button)
        action_row.addWidget(self.select_none_button)
        action_row.addStretch(1)

        bottom_row = QHBoxLayout()
        bottom_row.addWidget(self.overwrite_check)
        bottom_row.addStretch(1)
        cancel_button = QPushButton("取消")
        cancel_button.clicked.connect(self.reject)
        self.restore_button = QPushButton("开始还原")
        self.restore_button.setDefault(True)
        self.restore_button.clicked.connect(self.start_restore)
        bottom_row.addWidget(cancel_button)
        bottom_row.addWidget(self.restore_button)

        layout = QVBoxLayout(self)
        layout.addLayout(form)
        layout.addLayout(action_row)
        layout.addWidget(self.tree, 1)
        layout.addLayout(bottom_row)

    # ---------- 交互 ----------
    def choose_archive(self) -> None:
        path, _ = QFileDialog.getOpenFileName(self, "选择备份档案", str(Path.home()),
                                              "DataGuard 档案 (*.bkpk);;所有文件 (*.*)")
        if path:
            self.archive_edit.setText(path)

    def load_entries(self) -> None:
        archive = self.archive_edit.text().strip()
        if not archive:
            QMessageBox.warning(self, "缺少档案", "请先选择 .bkpk 档案")
            return
        entries = self.client.list_archive(archive, self.password_edit.text())
        if not entries:
            QMessageBox.warning(self, "无法读取", "档案无法读取或为空（加密档案请先填写口令）")
            return
        self.populate(entries)

    def populate(self, entries: list[dict]) -> None:
        self.tree.clear()
        nodes: dict[str, QTreeWidgetItem] = {}
        for entry in entries:
            parts = entry["path"].split("/")
            parent_item = None
            for index, part in enumerate(parts):
                key = "/".join(parts[:index + 1])
                item = nodes.get(key)
                if item is None:
                    item = QTreeWidgetItem(parent_item if parent_item else self.tree)
                    item.setText(0, part)
                    item.setCheckState(0, Qt.Checked)
                    item.setData(0, Qt.UserRole, key if index == len(parts) - 1 else "")
                    if index == len(parts) - 1:
                        item.setText(1, entry.get("size", ""))
                    nodes[key] = item
                parent_item = item
        self.tree.expandAll()

    def set_all_checked(self, checked: bool) -> None:
        state = Qt.Checked if checked else Qt.Unchecked

        def walk(item: QTreeWidgetItem) -> None:
            item.setCheckState(0, state)
            for index in range(item.childCount()):
                walk(item.child(index))

        for index in range(self.tree.topLevelItemCount()):
            walk(self.tree.topLevelItem(index))

    def collect_selected(self) -> list[str]:
        selected: list[str] = []

        def walk(item: QTreeWidgetItem) -> None:
            key = item.data(0, Qt.UserRole) or ""
            if key and item.checkState(0) == Qt.Checked:
                selected.append(key)
            for index in range(item.childCount()):
                walk(item.child(index))

        for index in range(self.tree.topLevelItemCount()):
            walk(self.tree.topLevelItem(index))
        return selected

    def start_restore(self) -> None:
        archive = self.archive_edit.text().strip()
        dest = self.dest_edit.text().strip()
        if not archive:
            QMessageBox.warning(self, "缺少档案", "请先选择 .bkpk 档案")
            return
        if not dest:
            QMessageBox.warning(self, "缺少目标目录", "请选择还原目标目录")
            return

        files = self.collect_selected()
        self.progress_dialog = ProgressDialog("还原进度", f"档案：{archive}", self)
        self.progress_dialog.close_button.setEnabled(False)

        self.worker = CliWorker(self.client, "restore", {
            "archive": archive,
            "dest": dest,
            "password": self.password_edit.text(),
            "overwrite": self.overwrite_check.isChecked(),
            "files": files or None,
        }, self)
        self.worker.progress.connect(self.progress_dialog.on_progress)
        self.worker.log.connect(self.progress_dialog.on_log)
        self.worker.done.connect(self.on_restore_done)
        self.worker.start()
        self.progress_dialog.show()

    def on_restore_done(self, ok: bool, message: str) -> None:
        if self.progress_dialog is not None:
            self.progress_dialog.finish(ok, message)

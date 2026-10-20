"""还原对话框：选择档案 → 勾选条目 → 还原到目标目录。

勾选逻辑全部交给 ``selection_model.SelectionTree``（纯逻辑、可单测），
本文件只负责渲染与交互：
- 勾选父节点联动整棵子树，子树全选/全不选时父节点自动跟随；
- 完全选中的子树只以「目录」形式进入还原清单，避免条目数炸开；
- 全选等价于整档还原，不再向 CLI 传条目清单。
"""

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
    QLabel,
    QLineEdit,
    QMessageBox,
    QPushButton,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from cli_client import CliClient
from selection_model import EntryNode, SelectionTree, State
from ui_common import CliWorker, ProgressDialog


def qt_state(value: int):
    """把模型里的 0/1/2 转成 Qt.CheckState（PySide6 对参数类型校验严格）。"""
    return Qt.CheckState(value)


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

        self.model = SelectionTree()
        self._items: dict[str, QTreeWidgetItem] = {}
        self._syncing = False

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
        self.tree.itemChanged.connect(self._on_item_changed)

        self.count_label = QLabel("尚未加载条目")
        self.count_label.setStyleSheet("color: #666;")
        self.hint_label = QLabel("提示：勾选目录会连同其下所有文件一起还原")
        self.hint_label.setStyleSheet("color: #888;")

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
        action_row.addSpacing(12)
        action_row.addWidget(self.count_label)
        action_row.addStretch(1)

        self.overwrite_check = QCheckBox("覆盖已存在的文件")
        self.overwrite_check.setChecked(True)

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
        layout.addWidget(self.hint_label)
        layout.addWidget(self.tree, 1)
        layout.addLayout(bottom_row)

    # ---------- 数据加载 ----------
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
        """用档案条目重建选择模型与树控件。"""
        self.model = SelectionTree(entries)
        self._render()

    # ---------- 渲染 ----------
    def _render(self) -> None:
        self._syncing = True
        try:
            self.tree.clear()
            self._items.clear()
            for node in self.model.top_level():
                self._render_node(node, None)
            self.tree.expandAll()
        finally:
            self._syncing = False
        self._refresh_count()

    def _render_node(self, node: EntryNode, parent_item: QTreeWidgetItem | None) -> None:
        item = QTreeWidgetItem(parent_item if parent_item is not None else self.tree)
        item.setText(0, node.name)
        if node.is_entry and node.size:
            item.setText(1, node.size)
        item.setFlags(item.flags() | Qt.ItemIsUserCheckable)
        item.setCheckState(0, qt_state(node.state()))
        item.setData(0, Qt.UserRole, node.key)
        self._items[node.key] = item
        for child in node.children:
            self._render_node(child, item)

    def _sync_states(self, node: EntryNode) -> None:
        item = self._items.get(node.key)
        if item is not None:
            item.setCheckState(0, qt_state(node.state()))
        for child in node.children:
            self._sync_states(child)

    # ---------- 交互 ----------
    def _on_item_changed(self, item: QTreeWidgetItem, column: int) -> None:
        if self._syncing or column != 0:
            return
        key = item.data(0, Qt.UserRole) or ""
        node = self.model.node(key)
        if node is None:
            return

        state = item.checkState(0)
        if state == Qt.CheckState.PartiallyChecked:
            return  # 半选是派生状态，不接受直接操作

        self._syncing = True
        try:
            self.model.set_checked(node, state == Qt.CheckState.Checked)
            self._sync_states(self.model.root)
        finally:
            self._syncing = False
        self._refresh_count()

    def set_all_checked(self, checked: bool) -> None:
        self.model.set_all(checked)
        self._syncing = True
        try:
            self._sync_states(self.model.root)
        finally:
            self._syncing = False
        self._refresh_count()

    def _refresh_count(self) -> None:
        total = self.model.entry_count
        if total == 0:
            self.count_label.setText("尚未加载条目")
            return
        selected = self.model.selected_count()
        if selected == 0:
            self.count_label.setText(f"已选 0/{total} 项")
            return
        paths = len(self.model.selected_paths())
        self.count_label.setText(f"已选 {selected}/{total} 项（清单 {paths} 项）")

    # ---------- 执行 ----------
    def selected_paths(self) -> list[str]:
        return self.model.selected_paths()

    def start_restore(self) -> None:
        archive = self.archive_edit.text().strip()
        dest = self.dest_edit.text().strip()
        if not archive:
            QMessageBox.warning(self, "缺少档案", "请先选择 .bkpk 档案")
            return
        if not dest:
            QMessageBox.warning(self, "缺少目标目录", "请选择还原目标目录")
            return
        if self.model.entry_count == 0:
            QMessageBox.warning(self, "缺少条目", "请先点“加载清单”读取档案内容")
            return
        if self.model.none_selected():
            QMessageBox.warning(self, "未选择条目", "请至少勾选一个条目（或点“全选”）")
            return

        # 全选等价于整档还原：不传清单，最快且不触碰命令行长度限制
        files = None if self.model.all_selected() else self.model.selected_paths()

        self.progress_dialog = ProgressDialog("还原进度", f"档案：{archive}", self)
        self.progress_dialog.close_button.setEnabled(False)

        self.worker = CliWorker(self.client, "restore", {
            "archive": archive,
            "dest": dest,
            "password": self.password_edit.text(),
            "overwrite": self.overwrite_check.isChecked(),
            "files": files,
        }, self)
        self.worker.progress.connect(self.progress_dialog.on_progress)
        self.worker.log.connect(self.progress_dialog.on_log)
        self.worker.done.connect(self.on_restore_done)
        self.worker.start()
        self.progress_dialog.show()

    def on_restore_done(self, ok: bool, message: str) -> None:
        if self.progress_dialog is not None:
            self.progress_dialog.finish(ok, message)

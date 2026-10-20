"""任务管理：任务列表 + 启用/禁用、编辑、删除、立即执行。"""

from __future__ import annotations

from qt_bootstrap import ensure as _ensure_qt_runtime

_ensure_qt_runtime()

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QAbstractItemView,
    QHBoxLayout,
    QMessageBox,
    QPushButton,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from backup_wizard import BackupWizard
from tasks import TaskStore, describe_retention, describe_schedule


class TaskManagerWidget(QWidget):
    """任务管理页面。"""

    taskRunRequested = Signal(dict)
    taskEdited = Signal()

    def __init__(self, store: TaskStore, parent=None):
        super().__init__(parent)
        self.store = store

        self.table = QTableWidget(0, 6)
        self.table.setHorizontalHeaderLabels(
            ["任务名", "计划", "源目录", "目标目录", "保留策略", "状态"])
        self.table.setSelectionBehavior(QAbstractItemView.SelectRows)
        self.table.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self.table.setColumnWidth(0, 120)
        self.table.setColumnWidth(1, 150)
        self.table.setColumnWidth(2, 200)
        self.table.setColumnWidth(3, 200)
        self.table.setColumnWidth(4, 150)

        button_row = QHBoxLayout()
        self.run_button = QPushButton("立即执行")
        self.run_button.clicked.connect(self.run_selected)
        self.toggle_button = QPushButton("启用/禁用")
        self.toggle_button.clicked.connect(self.toggle_selected)
        self.edit_button = QPushButton("编辑")
        self.edit_button.clicked.connect(self.edit_selected)
        self.delete_button = QPushButton("删除")
        self.delete_button.clicked.connect(self.delete_selected)
        self.refresh_button = QPushButton("刷新")
        self.refresh_button.clicked.connect(self.refresh)

        for button in (self.run_button, self.toggle_button, self.edit_button,
                       self.delete_button):
            button_row.addWidget(button)
        button_row.addStretch(1)
        button_row.addWidget(self.refresh_button)

        layout = QVBoxLayout(self)
        layout.addWidget(self.table, 1)
        layout.addLayout(button_row)

        self.refresh()

    # ---------- 数据 ----------
    def refresh(self) -> None:
        self.table.setRowCount(0)
        for task in self.store.tasks:
            row = self.table.rowCount()
            self.table.insertRow(row)

            name_item = QTableWidgetItem(task.get("name", ""))
            name_item.setData(Qt.UserRole, task.get("id", ""))
            self.table.setItem(row, 0, name_item)
            self.table.setItem(row, 1, QTableWidgetItem(
                describe_schedule(task.get("schedule", {}))))
            self.table.setItem(row, 2, QTableWidgetItem(task.get("source", "")))
            self.table.setItem(row, 3, QTableWidgetItem(task.get("dest_dir", "")))
            self.table.setItem(row, 4, QTableWidgetItem(
                describe_retention(task.get("retention", {}))))
            self.table.setItem(row, 5, QTableWidgetItem(
                "已启用" if task.get("enabled", True) else "已禁用"))

    def selected_task(self) -> dict | None:
        row = self.table.currentRow()
        if row < 0:
            return None
        item = self.table.item(row, 0)
        if item is None:
            return None
        task_id = item.data(Qt.UserRole)
        return self.store.get(task_id)

    # ---------- 操作 ----------
    def run_selected(self) -> None:
        task = self.selected_task()
        if task is None:
            QMessageBox.information(self, "未选择任务", "请先在表格中选择一个任务")
            return
        self.taskRunRequested.emit(task)

    def toggle_selected(self) -> None:
        task = self.selected_task()
        if task is None:
            QMessageBox.information(self, "未选择任务", "请先在表格中选择一个任务")
            return
        self.store.set_enabled(task["id"], not task.get("enabled", True))
        self.store.save()
        self.refresh()

    def edit_selected(self) -> None:
        task = self.selected_task()
        if task is None:
            QMessageBox.information(self, "未选择任务", "请先在表格中选择一个任务")
            return

        wizard = BackupWizard(self, initial=task)
        if wizard.exec() == BackupWizard.Accepted:
            updated = wizard.result_config()
            updated["id"] = task.get("id", "")
            updated["enabled"] = task.get("enabled", True)
            self.store.update(updated)
            self.store.save()
            self.refresh()
            self.taskEdited.emit()

    def delete_selected(self) -> None:
        task = self.selected_task()
        if task is None:
            QMessageBox.information(self, "未选择任务", "请先在表格中选择一个任务")
            return
        confirm = QMessageBox.question(
            self, "确认删除", f"确定删除任务「{task.get('name', '')}」吗？")
        if confirm != QMessageBox.Yes:
            return
        self.store.remove(task["id"])
        self.store.save()
        self.refresh()

"""DataGuard 主窗口。"""

from __future__ import annotations

from pathlib import Path

from qt_bootstrap import ensure as _ensure_qt_runtime

_ensure_qt_runtime()

from PySide6.QtCore import Qt
from PySide6.QtGui import QAction
from PySide6.QtWidgets import (
    QAbstractItemView,
    QFileDialog,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QStackedWidget,
    QTableWidget,
    QTableWidgetItem,
    QToolBar,
    QVBoxLayout,
    QWidget,
)

from backup_wizard import BackupWizard
from cli_client import CliClient
from restore_dialog import RestoreDialog
from scheduler import Scheduler
from task_manager import TaskManagerWidget
from tasks import TaskStore
from ui_common import CliWorker, ProgressDialog

DEFAULT_ARCHIVE_DIR = Path.home() / "DataGuardBackups"


def format_size(size: int) -> str:
    units = ["B", "KB", "MB", "GB"]
    value = float(size)
    for unit in units:
        if value < 1024 or unit == units[-1]:
            return f"{value:.1f} {unit}" if unit != "B" else f"{int(value)} B"
        value /= 1024
    return f"{size} B"


def parse_archive_name(path: Path) -> str:
    """从 <任务名>_<yyyyMMdd_HHmmss>.bkpk 中解析任务名。"""
    parts = path.stem.rsplit("_", 2)
    return parts[0] if len(parts) >= 3 else path.stem


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("DataGuard 数据备份软件")
        self.resize(1040, 660)

        self.client = CliClient()
        self.store = TaskStore()
        self.store.load()

        self.archive_dir = DEFAULT_ARCHIVE_DIR
        try:
            self.archive_dir.mkdir(parents=True, exist_ok=True)
        except OSError:
            self.archive_dir = Path.cwd()

        self.scheduler = Scheduler(self.store, self)
        self.scheduler.taskDue.connect(self.on_task_due)
        self.scheduler.start()

        self.worker: CliWorker | None = None
        self.progress_dialog: ProgressDialog | None = None
        self.current_task: dict | None = None

        self.build_ui()
        self.refresh_history()
        self.update_status()

    # ---------- 界面 ----------
    def build_ui(self) -> None:
        self.nav = QListWidget()
        self.nav.addItems(["备份历史", "任务管理", "设置"])
        self.nav.setFixedWidth(150)
        self.nav.setCurrentRow(0)
        self.nav.currentRowChanged.connect(self.on_nav_changed)

        self.pages = QStackedWidget()
        self.pages.addWidget(self.build_history_page())

        self.task_manager = TaskManagerWidget(self.store)
        self.task_manager.taskRunRequested.connect(self.run_task)
        self.pages.addWidget(self.task_manager)

        self.pages.addWidget(self.build_settings_page())

        central = QWidget()
        layout = QHBoxLayout(central)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(self.nav)
        layout.addWidget(self.pages, 1)
        self.setCentralWidget(central)

        toolbar = QToolBar("主工具栏")
        toolbar.setMovable(False)
        self.addToolBar(toolbar)

        new_action = QAction("新建备份", self)
        new_action.triggered.connect(self.new_backup)
        restore_action = QAction("还原", self)
        restore_action.triggered.connect(self.open_restore)
        refresh_action = QAction("刷新历史", self)
        refresh_action.triggered.connect(self.refresh_history)

        toolbar.addAction(new_action)
        toolbar.addAction(restore_action)
        toolbar.addAction(refresh_action)

        self.status_label = QLabel()
        self.statusBar().addPermanentWidget(self.status_label)

    def build_history_page(self) -> QWidget:
        page = QWidget()
        layout = QVBoxLayout(page)

        self.history_table = QTableWidget(0, 5)
        self.history_table.setHorizontalHeaderLabels(
            ["档案名", "任务名", "大小", "备份时间", "状态"])
        self.history_table.setSelectionBehavior(QAbstractItemView.SelectRows)
        self.history_table.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self.history_table.setColumnWidth(0, 360)
        self.history_table.setColumnWidth(1, 140)
        self.history_table.setColumnWidth(2, 100)
        self.history_table.setColumnWidth(3, 180)
        self.history_table.doubleClicked.connect(self.show_archive_details)

        button_row = QHBoxLayout()
        refresh_button = QPushButton("刷新")
        refresh_button.clicked.connect(self.refresh_history)
        open_dir_button = QPushButton("打开备份目录")
        open_dir_button.clicked.connect(self.open_archive_dir)
        button_row.addWidget(refresh_button)
        button_row.addWidget(open_dir_button)
        button_row.addStretch(1)

        layout.addWidget(QLabel(f"备份档案目录：{self.archive_dir}"))
        layout.addWidget(self.history_table, 1)
        layout.addLayout(button_row)
        return page

    def build_settings_page(self) -> QWidget:
        page = QWidget()
        layout = QVBoxLayout(page)

        self.archive_dir_edit = QLineEdit(str(self.archive_dir))
        choose_button = QPushButton("浏览…")
        choose_button.setFixedWidth(80)
        choose_button.clicked.connect(self.choose_archive_dir)
        row = QHBoxLayout()
        row.addWidget(self.archive_dir_edit, 1)
        row.addWidget(choose_button)

        save_button = QPushButton("保存设置")
        save_button.clicked.connect(self.save_settings)

        layout.addWidget(QLabel("默认备份目录"))
        layout.addLayout(row)
        layout.addWidget(save_button)
        layout.addWidget(QLabel(f"命令行程序：{self.client.cli_path or '未找到'}"))
        layout.addStretch(1)
        return page

    # ---------- 交互 ----------
    def on_nav_changed(self, index: int) -> None:
        self.pages.setCurrentIndex(index)
        if index == 1:
            self.task_manager.refresh()

    def choose_archive_dir(self) -> None:
        path = QFileDialog.getExistingDirectory(self, "选择备份目录",
                                                self.archive_dir_edit.text())
        if path:
            self.archive_dir_edit.setText(path)

    def save_settings(self) -> None:
        candidate = Path(self.archive_dir_edit.text().strip() or str(DEFAULT_ARCHIVE_DIR))
        try:
            candidate.mkdir(parents=True, exist_ok=True)
        except OSError as exc:
            QMessageBox.warning(self, "目录不可用", str(exc))
            return
        self.archive_dir = candidate
        self.refresh_history()
        self.update_status()
        QMessageBox.information(self, "已保存", "设置已保存")

    def open_archive_dir(self) -> None:
        QMessageBox.information(self, "备份目录", str(self.archive_dir))

    def refresh_history(self) -> None:
        self.history_table.setRowCount(0)
        try:
            files = sorted(self.archive_dir.glob("*.bkpk"),
                           key=lambda p: p.stat().st_mtime, reverse=True)
        except OSError:
            files = []

        for path in files[:200]:
            try:
                stat = path.stat()
            except OSError:
                continue
            row = self.history_table.rowCount()
            self.history_table.insertRow(row)
            self.history_table.setItem(row, 0, QTableWidgetItem(path.name))
            self.history_table.setItem(row, 1, QTableWidgetItem(parse_archive_name(path)))
            self.history_table.setItem(row, 2, QTableWidgetItem(format_size(stat.st_size)))
            import datetime
            mtime = datetime.datetime.fromtimestamp(stat.st_mtime).strftime("%Y-%m-%d %H:%M:%S")
            self.history_table.setItem(row, 3, QTableWidgetItem(mtime))
            self.history_table.setItem(row, 4, QTableWidgetItem("可用"))

    def show_archive_details(self) -> None:
        row = self.history_table.currentRow()
        if row < 0:
            return
        archive = self.archive_dir / self.history_table.item(row, 0).text()
        entries = self.client.list_archive(str(archive))
        if not entries:
            QMessageBox.information(self, "档案内容",
                                    "无法读取档案（加密档案请在还原对话框中查看）")
            return
        lines = [f"{entry['kind']}\t{entry['size']}\t{entry['path']}" for entry in entries[:50]]
        QMessageBox.information(self, f"档案内容（{len(entries)} 个条目）", "\n".join(lines))

    def new_backup(self) -> None:
        wizard = BackupWizard(self)
        if wizard.exec() != BackupWizard.Accepted:
            return
        config = wizard.result_config()
        if not config["source"] or not config["dest_dir"]:
            QMessageBox.warning(self, "信息不完整", "请填写源目录与目标目录")
            return

        schedule_type = config["schedule"].get("type", "manual")
        if schedule_type == "manual":
            self.run_backup_config(config)
        else:
            self.store.add(config)
            self.store.save()
            self.task_manager.refresh()
            QMessageBox.information(self, "任务已创建",
                                    "定时任务已保存，可在「任务管理」中查看")

    def open_restore(self) -> None:
        dialog = RestoreDialog(self.client, self)
        dialog.exec()

    def run_task(self, task: dict) -> None:
        self.run_backup_config(task)

    def run_backup_config(self, config: dict) -> None:
        dest = Path(config.get("dest_dir", ""))
        try:
            dest.mkdir(parents=True, exist_ok=True)
        except OSError as exc:
            QMessageBox.warning(self, "目标目录不可用", str(exc))
            return

        self.current_task = config
        options = config.get("options", {})
        self.progress_dialog = ProgressDialog(
            "备份进度", f"源：{config.get('source', '')}", self)
        self.progress_dialog.close_button.setEnabled(False)

        self.worker = CliWorker(self.client, "backup", {
            "source": config.get("source", ""),
            "dest": config.get("dest_dir", ""),
            "name": config.get("name", "backup"),
            "compress": bool(options.get("compress", False)),
            "encrypt": bool(options.get("encrypt", False)),
            "password": options.get("password", ""),
            "filters": config.get("filter", {}),
        }, self)
        self.worker.progress.connect(self.progress_dialog.on_progress)
        self.worker.log.connect(self.progress_dialog.on_log)
        self.worker.done.connect(self.on_backup_done)
        self.worker.start()
        self.progress_dialog.show()

    def on_backup_done(self, ok: bool, message: str) -> None:
        if self.progress_dialog is not None:
            self.progress_dialog.finish(ok, message)

        if ok and self.current_task is not None:
            retention = self.current_task.get("retention", {})
            keep_count = int(retention.get("keep_count", 0))
            keep_days = int(retention.get("keep_days", 0))
            if keep_count > 0 or keep_days > 0:
                self.client.retention(self.current_task.get("dest_dir", ""),
                                      self.current_task.get("name", "backup"),
                                      keep_count, keep_days)
        self.refresh_history()

    def on_task_due(self, task: dict) -> None:
        self.run_backup_config(task)

    def update_status(self) -> None:
        cli_text = str(self.client.cli_path) if self.client.cli_path else "未找到 dataguard-cli.exe"
        self.status_label.setText(f"CLI: {cli_text}    定时调度：运行中")

    def closeEvent(self, event) -> None:  # noqa: N802 - Qt 命名
        self.scheduler.stop()
        self.store.save()
        super().closeEvent(event)

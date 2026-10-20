"""新建备份向导（4 页）：源目标 → 筛选 → 选项 → 计划与保留。"""

from __future__ import annotations

from pathlib import Path

from qt_bootstrap import ensure as _ensure_qt_runtime

_ensure_qt_runtime()

from PySide6.QtCore import QDate, Qt, QTime
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QDateEdit,
    QFileDialog,
    QFormLayout,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMessageBox,
    QPushButton,
    QSpinBox,
    QTimeEdit,
    QVBoxLayout,
    QWidget,
    QWizard,
    QWizardPage,
)


def split_values(text: str) -> list[str]:
    """把逗号分隔的输入拆成列表（去除空项与空格）。"""
    return [item.strip() for item in text.replace("，", ",").split(",") if item.strip()]


def browse_widget(edit: QLineEdit, caption: str) -> QWidget:
    container = QWidget()
    layout = QHBoxLayout(container)
    layout.setContentsMargins(0, 0, 0, 0)
    button = QPushButton("浏览…")
    button.setFixedWidth(80)

    def choose() -> None:
        start = edit.text().strip() or str(Path.home())
        path = QFileDialog.getExistingDirectory(container, caption, start)
        if path:
            edit.setText(path)

    button.clicked.connect(choose)
    layout.addWidget(edit, 1)
    layout.addWidget(button)
    return container


class SourcePage(QWizardPage):
    def __init__(self):
        super().__init__()
        self.setTitle("第 1 步：源与目标")
        self.setSubTitle("选择要备份的目录、档案保存位置与任务名称")

        self.source_edit = QLineEdit()
        self.dest_edit = QLineEdit()
        self.name_edit = QLineEdit("backup")

        form = QFormLayout(self)
        form.addRow("源目录", browse_widget(self.source_edit, "选择源目录"))
        form.addRow("目标目录", browse_widget(self.dest_edit, "选择目标目录"))
        form.addRow("任务名称", self.name_edit)
        form.addRow(QLabel("提示：档案命名为 <任务名>_<时间戳>.bkpk"))


class FilterPage(QWizardPage):
    def __init__(self):
        super().__init__()
        self.setTitle("第 2 步：筛选条件")
        self.setSubTitle("留空表示不限制；多个值用逗号分隔")

        self.include_ext = QLineEdit()
        self.include_ext.setPlaceholderText("例如：.cpp,.h")
        self.exclude_ext = QLineEdit()
        self.exclude_ext.setPlaceholderText("例如：.tmp,.log")
        self.include_glob = QLineEdit()
        self.include_glob.setPlaceholderText("例如：src/*")
        self.exclude_glob = QLineEdit()
        self.exclude_glob.setPlaceholderText("例如：*/build/*")
        self.include_name = QLineEdit()
        self.include_name.setPlaceholderText("例如：report_*")
        self.modified_after = QLineEdit()
        self.modified_after.setPlaceholderText("YYYY-MM-DD（可空）")
        self.owner = QLineEdit()
        self.owner.setPlaceholderText("属主用户名，可空")

        self.min_size = QSpinBox()
        self.min_size.setRange(0, 1024 * 1024)
        self.min_size.setSuffix(" MB")
        self.max_size = QSpinBox()
        self.max_size.setRange(0, 1024 * 1024)
        self.max_size.setSuffix(" MB")

        self.follow_symlinks = QCheckBox("跟随符号链接目录")

        form = QFormLayout(self)
        form.addRow("包含扩展名", self.include_ext)
        form.addRow("排除扩展名", self.exclude_ext)
        form.addRow("路径包含（glob）", self.include_glob)
        form.addRow("路径排除（glob）", self.exclude_glob)
        form.addRow("文件名匹配", self.include_name)
        form.addRow("最早修改日期", self.modified_after)
        form.addRow("最小尺寸", self.min_size)
        form.addRow("最大尺寸", self.max_size)
        form.addRow("属主", self.owner)
        form.addRow("", self.follow_symlinks)


class OptionsPage(QWizardPage):
    def __init__(self):
        super().__init__()
        self.setTitle("第 3 步：压缩与加密")
        self.setSubTitle("压缩使用自研 DEFLATE，加密使用自研流密码")

        self.compress = QCheckBox("启用 DEFLATE 压缩")
        self.encrypt = QCheckBox("启用加密（需要口令）")
        self.password = QLineEdit()
        self.password.setEchoMode(QLineEdit.Password)
        self.confirm = QLineEdit()
        self.confirm.setEchoMode(QLineEdit.Password)

        layout = QVBoxLayout(self)
        layout.addWidget(self.compress)
        layout.addWidget(self.encrypt)

        form = QFormLayout()
        form.addRow("口令", self.password)
        form.addRow("确认口令", self.confirm)
        layout.addLayout(form)
        layout.addStretch(1)

        self.encrypt.toggled.connect(self._update_enabled)
        self._update_enabled(False)

    def _update_enabled(self, enabled: bool) -> None:
        self.password.setEnabled(enabled)
        self.confirm.setEnabled(enabled)

    def validatePage(self) -> bool:
        if not self.encrypt.isChecked():
            return True
        if not self.password.text():
            QMessageBox.warning(self, "口令为空", "启用加密时必须填写口令")
            return False
        if self.password.text() != self.confirm.text():
            QMessageBox.warning(self, "口令不一致", "两次输入的口令不一致")
            return False
        return True


class SchedulePage(QWizardPage):
    def __init__(self):
        super().__init__()
        self.setTitle("第 4 步：计划与保留")
        self.setSubTitle("所有参数由你设置，程序不会写死任何周期")

        self.mode = QComboBox()
        self.mode.addItems(["立即执行", "每天", "每周", "每月", "一次性"])
        self.time_edit = QTimeEdit(QTime(8, 0))
        self.time_edit.setDisplayFormat("HH:mm")
        self.weekday = QComboBox()
        self.weekday.addItems(["周一", "周二", "周三", "周四", "周五", "周六", "周日"])
        self.day_spin = QSpinBox()
        self.day_spin.setRange(1, 31)
        self.run_at = QDateEdit()
        self.run_at.setDisplayFormat("yyyy-MM-dd")
        self.run_at.setDate(QDate.currentDate().addDays(1))

        self.retention_mode = QComboBox()
        self.retention_mode.addItems(["不淘汰", "保留最近 N 份", "保留最近 N 天"])
        self.retention_value = QSpinBox()
        self.retention_value.setRange(1, 999)
        self.retention_value.setValue(5)

        form = QFormLayout(self)
        form.addRow("执行方式", self.mode)
        form.addRow("触发时刻", self.time_edit)
        form.addRow("每周几", self.weekday)
        form.addRow("每月几号", self.day_spin)
        form.addRow("一次性日期", self.run_at)
        form.addRow("保留策略", self.retention_mode)
        form.addRow("保留数量", self.retention_value)

        self.mode.currentIndexChanged.connect(self._update_visibility)
        self.retention_mode.currentIndexChanged.connect(self._update_visibility)
        self._update_visibility()

    def _update_visibility(self) -> None:
        mode = self.mode.currentText()
        self.time_edit.setEnabled(mode in ("每天", "每周", "每月"))
        self.weekday.setEnabled(mode == "每周")
        self.day_spin.setEnabled(mode == "每月")
        self.run_at.setEnabled(mode == "一次性")
        self.retention_value.setEnabled(self.retention_mode.currentText() != "不淘汰")

    def schedule_config(self) -> dict:
        mode = self.mode.currentText()
        time_value = self.time_edit.time()
        if mode == "立即执行":
            schedule_type = "manual"
        elif mode == "每天":
            schedule_type = "daily"
        elif mode == "每周":
            schedule_type = "weekly"
        elif mode == "每月":
            schedule_type = "monthly"
        else:
            schedule_type = "once"

        return {
            "type": schedule_type,
            "hour": time_value.hour(),
            "minute": time_value.minute(),
            "weekday": self.weekday.currentIndex() + 1,
            "day_of_month": self.day_spin.value(),
            "run_at": f"{self.run_at.date().toString('yyyy-MM-dd')} "
                      f"{time_value.toString('HH:mm')}",
        }

    def retention_config(self) -> dict:
        mode = self.retention_mode.currentText()
        value = self.retention_value.value()
        if mode == "保留最近 N 份":
            return {"keep_count": value, "keep_days": 0}
        if mode == "保留最近 N 天":
            return {"keep_count": 0, "keep_days": value}
        return {"keep_count": 0, "keep_days": 0}


class BackupWizard(QWizard):
    """新建（或编辑）备份任务的向导。"""

    def __init__(self, parent=None, initial: dict | None = None):
        super().__init__(parent)
        self.setWindowTitle("新建备份任务")
        self.setWizardStyle(QWizard.ModernStyle)
        self.resize(640, 480)

        self.source_page = SourcePage()
        self.filter_page = FilterPage()
        self.options_page = OptionsPage()
        self.schedule_page = SchedulePage()
        self.addPage(self.source_page)
        self.addPage(self.filter_page)
        self.addPage(self.options_page)
        self.addPage(self.schedule_page)

        if initial:
            self.apply_initial(initial)

    def apply_initial(self, task: dict) -> None:
        """用已有任务预填向导（编辑场景）。"""
        self.source_page.source_edit.setText(task.get("source", ""))
        self.source_page.dest_edit.setText(task.get("dest_dir", ""))
        self.source_page.name_edit.setText(task.get("name", "backup"))

        filters = task.get("filter", {})
        self.filter_page.include_ext.setText(",".join(filters.get("include_exts", [])))
        self.filter_page.exclude_ext.setText(",".join(filters.get("exclude_exts", [])))
        self.filter_page.include_glob.setText(",".join(filters.get("include_path_globs", [])))
        self.filter_page.exclude_glob.setText(",".join(filters.get("exclude_path_globs", [])))
        self.filter_page.include_name.setText(",".join(filters.get("include_name_globs", [])))
        self.filter_page.modified_after.setText(str(filters.get("modified_after", "") or ""))

        options = task.get("options", {})
        self.options_page.compress.setChecked(bool(options.get("compress", False)))
        self.options_page.encrypt.setChecked(bool(options.get("encrypt", False)))
        self.options_page.password.setText(options.get("password", ""))
        self.options_page.confirm.setText(options.get("password", ""))

        schedule = task.get("schedule", {})
        mode_map = {"manual": "立即执行", "daily": "每天", "weekly": "每周",
                    "monthly": "每月", "once": "一次性"}
        self.schedule_page.mode.setCurrentText(mode_map.get(schedule.get("type", "manual"), "立即执行"))
        self.schedule_page.time_edit.setTime(
            QTime(int(schedule.get("hour", 8)), int(schedule.get("minute", 0))))
        self.schedule_page.weekday.setCurrentIndex(max(0, int(schedule.get("weekday", 1)) - 1))
        self.schedule_page.day_spin.setValue(int(schedule.get("day_of_month", 1)))

        retention = task.get("retention", {})
        if int(retention.get("keep_count", 0)) > 0:
            self.schedule_page.retention_mode.setCurrentText("保留最近 N 份")
            self.schedule_page.retention_value.setValue(int(retention["keep_count"]))
        elif int(retention.get("keep_days", 0)) > 0:
            self.schedule_page.retention_mode.setCurrentText("保留最近 N 天")
            self.schedule_page.retention_value.setValue(int(retention["keep_days"]))

    def result_config(self) -> dict:
        """把向导输入转换为任务配置字典。"""
        filters = {
            "include_exts": split_values(self.filter_page.include_ext.text()),
            "exclude_exts": split_values(self.filter_page.exclude_ext.text()),
            "include_path_globs": split_values(self.filter_page.include_glob.text()),
            "exclude_path_globs": split_values(self.filter_page.exclude_glob.text()),
            "include_name_globs": split_values(self.filter_page.include_name.text()),
            "include_owners": split_values(self.filter_page.owner.text()),
            "modified_after": self.filter_page.modified_after.text().strip(),
            "modified_before": "",
            "min_size": self.filter_page.min_size.value() * 1024 * 1024
            if self.filter_page.min_size.value() > 0 else -1,
            "max_size": self.filter_page.max_size.value() * 1024 * 1024
            if self.filter_page.max_size.value() > 0 else -1,
            "follow_symlinks": self.filter_page.follow_symlinks.isChecked(),
        }

        options = {
            "compress": self.options_page.compress.isChecked(),
            "encrypt": self.options_page.encrypt.isChecked(),
            "password": self.options_page.password.text(),
        }

        return {
            "name": self.source_page.name_edit.text().strip() or "backup",
            "source": self.source_page.source_edit.text().strip(),
            "dest_dir": self.source_page.dest_edit.text().strip(),
            "filter": filters,
            "options": options,
            "schedule": self.schedule_page.schedule_config(),
            "retention": self.schedule_page.retention_config(),
        }

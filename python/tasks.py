"""任务配置模型与持久化（dataguard_tasks.json）。"""

from __future__ import annotations

import json
import time
import uuid
from pathlib import Path
from typing import Optional

DEFAULT_TASKS_FILE = Path(__file__).resolve().parent / "dataguard_tasks.json"


def new_task_id() -> str:
    return f"{int(time.time() * 1000):x}{uuid.uuid4().hex[:6]}"


def default_filter() -> dict:
    return {
        "include_exts": [],
        "exclude_exts": [],
        "include_path_globs": [],
        "exclude_path_globs": [],
        "include_name_globs": [],
        "exclude_name_globs": [],
        "modified_after": "",
        "modified_before": "",
        "min_size": -1,
        "max_size": -1,
        "include_owners": [],
        "follow_symlinks": False,
    }


def default_task() -> dict:
    return {
        "id": "",
        "name": "backup",
        "source": "",
        "dest_dir": "",
        "filter": default_filter(),
        "options": {"compress": False, "encrypt": False, "password": ""},
        "schedule": {
            "type": "manual",
            "hour": 8,
            "minute": 0,
            "weekday": 1,
            "day_of_month": 1,
            "run_at": "",
        },
        "retention": {"keep_count": 0, "keep_days": 0},
        "enabled": True,
    }


def fill_defaults(task: dict) -> dict:
    """补齐缺省字段（兼容旧版配置文件）。"""
    base = default_task()
    for key, value in base.items():
        if key not in task:
            task[key] = value
        elif isinstance(value, dict) and isinstance(task.get(key), dict):
            for sub_key, sub_value in value.items():
                task[key].setdefault(sub_key, sub_value)
    if not task.get("id"):
        task["id"] = new_task_id()
    return task


class TaskStore:
    """任务列表的内存模型 + JSON 持久化。"""

    def __init__(self, path: Optional[Path] = None):
        self.path = Path(path) if path else DEFAULT_TASKS_FILE
        self.tasks: list[dict] = []

    def load(self) -> bool:
        if not self.path.is_file():
            self.tasks = []
            return True
        try:
            data = json.loads(self.path.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, OSError):
            # 损坏的配置文件备份为 .bak，避免阻塞启动
            try:
                self.path.replace(self.path.with_suffix(".bak"))
            except OSError:
                pass
            self.tasks = []
            return False

        if isinstance(data, dict):
            data = data.get("tasks", [])
        if not isinstance(data, list):
            self.tasks = []
            return False

        self.tasks = [fill_defaults(dict(item)) for item in data if isinstance(item, dict)]
        return True

    def save(self) -> bool:
        try:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            self.path.write_text(
                json.dumps(self.tasks, ensure_ascii=False, indent=2),
                encoding="utf-8",
            )
            return True
        except OSError:
            return False

    def add(self, task: dict) -> dict:
        item = fill_defaults(dict(task))
        if not item.get("id"):
            item["id"] = new_task_id()
        self.tasks.append(item)
        return item

    def get(self, task_id: str) -> Optional[dict]:
        for task in self.tasks:
            if task.get("id") == task_id:
                return task
        return None

    def remove(self, task_id: str) -> bool:
        for index, task in enumerate(self.tasks):
            if task.get("id") == task_id:
                del self.tasks[index]
                return True
        return False

    def update(self, task: dict) -> bool:
        for index, existing in enumerate(self.tasks):
            if existing.get("id") == task.get("id"):
                self.tasks[index] = fill_defaults(dict(task))
                return True
        return False

    def set_enabled(self, task_id: str, enabled: bool) -> bool:
        task = self.get(task_id)
        if task is None:
            return False
        task["enabled"] = bool(enabled)
        return True


def describe_schedule(schedule: dict) -> str:
    """把计划配置转成中文描述，用于任务列表展示。"""
    stype = (schedule or {}).get("type", "manual")
    hour = int(schedule.get("hour", 0))
    minute = int(schedule.get("minute", 0))
    clock = f"{hour:02d}:{minute:02d}"

    if stype == "manual":
        return "手动执行"
    if stype == "once":
        return f"一次性 {schedule.get('run_at', '')}"
    if stype == "daily":
        return f"每天 {clock}"
    if stype == "weekly":
        weekday_names = ["周一", "周二", "周三", "周四", "周五", "周六", "周日"]
        index = int(schedule.get("weekday", 1)) - 1
        index = max(0, min(6, index))
        return f"每周{weekday_names[index][1:]} {clock}"
    if stype == "monthly":
        return f"每月 {int(schedule.get('day_of_month', 1))} 日 {clock}"
    return "未知计划"


def describe_retention(retention: dict) -> str:
    retention = retention or {}
    keep_count = int(retention.get("keep_count", 0))
    keep_days = int(retention.get("keep_days", 0))
    parts = []
    if keep_count > 0:
        parts.append(f"保留最近 {keep_count} 份")
    if keep_days > 0:
        parts.append(f"保留最近 {keep_days} 天")
    return "；".join(parts) if parts else "不淘汰"

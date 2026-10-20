"""定时调度：判断任务是否到期，并通过 Qt 信号通知界面执行。"""

from __future__ import annotations

from datetime import datetime
from typing import Optional

from qt_bootstrap import ensure as _ensure_qt_runtime

_ensure_qt_runtime()

from PySide6.QtCore import QObject, QTimer, Signal


def is_due(task: dict, now: Optional[datetime] = None,
           last_fired: Optional[datetime] = None) -> bool:
    """判断任务在 ``now`` 时刻是否到期。

    规则：
      - manual：不触发；
      - once：到达 run_at 且未触发过；
      - daily：到达设定时刻且当天未触发；
      - weekly：星期匹配 + 到达时刻 + 当天未触发；
      - monthly：日期匹配 + 到达时刻 + 当天未触发。
    """
    if not task.get("enabled", True):
        return False

    schedule = task.get("schedule") or {}
    schedule_type = schedule.get("type", "manual")
    if schedule_type == "manual":
        return False

    now = now or datetime.now()

    if schedule_type == "once":
        run_at = schedule.get("run_at", "")
        if not run_at:
            return False
        try:
            target = datetime.strptime(run_at, "%Y-%m-%d %H:%M")
        except ValueError:
            return False
        return now >= target and last_fired is None

    hour = int(schedule.get("hour", 0))
    minute = int(schedule.get("minute", 0))
    if (now.hour, now.minute) < (hour, minute):
        return False

    if last_fired is not None and last_fired.date() == now.date():
        return False

    if schedule_type == "daily":
        return True
    if schedule_type == "weekly":
        return now.isoweekday() == int(schedule.get("weekday", 1))
    if schedule_type == "monthly":
        return now.day == int(schedule.get("day_of_month", 1))
    return False


class Scheduler(QObject):
    """每分钟检查一次任务到期情况，到期时发射 ``taskDue``。"""

    taskDue = Signal(dict)

    def __init__(self, store, parent=None):
        super().__init__(parent)
        self.store = store
        self._last_fired: dict[str, datetime] = {}
        self._timer = QTimer(self)
        self._timer.setInterval(60_000)
        self._timer.timeout.connect(self.tick)

    def start(self) -> None:
        self._timer.start()

    def stop(self) -> None:
        self._timer.stop()

    def is_running(self) -> bool:
        return self._timer.isActive()

    def tick(self, now: Optional[datetime] = None) -> list[dict]:
        """执行一次到期检查，返回本次触发的任务列表。"""
        current = now or datetime.now()
        fired: list[dict] = []

        for task in list(self.store.tasks):
            task_id = task.get("id", "")
            if not is_due(task, current, self._last_fired.get(task_id)):
                continue
            self._last_fired[task_id] = current
            fired.append(task)
            if (task.get("schedule") or {}).get("type") == "once":
                self.store.remove(task_id)

        if fired:
            self.store.save()

        for task in fired:
            self.taskDue.emit(task)
        return fired

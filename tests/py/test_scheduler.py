"""定时调度到期判定测试。"""

import sys
import unittest
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "python"))

from scheduler import Scheduler, is_due  # noqa: E402
from tasks import TaskStore  # noqa: E402


def make_task(schedule_type, **schedule):
    base = {"type": schedule_type, "hour": 8, "minute": 0, "weekday": 1,
            "day_of_month": 1, "run_at": ""}
    base.update(schedule)
    return {"id": "t1", "name": "demo", "enabled": True, "schedule": base}


class IsDueTest(unittest.TestCase):
    def test_manual_never_due(self):
        self.assertFalse(is_due(make_task("manual"), datetime(2026, 10, 20, 9, 0)))

    def test_disabled_task_not_due(self):
        task = make_task("daily")
        task["enabled"] = False
        self.assertFalse(is_due(task, datetime(2026, 10, 20, 9, 0)))

    def test_daily_before_time(self):
        task = make_task("daily", hour=8, minute=0)
        self.assertFalse(is_due(task, datetime(2026, 10, 20, 7, 59)))

    def test_daily_at_time(self):
        task = make_task("daily", hour=8, minute=0)
        self.assertTrue(is_due(task, datetime(2026, 10, 20, 8, 0)))

    def test_daily_not_twice_same_day(self):
        task = make_task("daily", hour=8, minute=0)
        now = datetime(2026, 10, 20, 9, 0)
        self.assertFalse(is_due(task, now, last_fired=datetime(2026, 10, 20, 8, 1)))

    def test_weekly_matching_weekday(self):
        # 2026-10-20 是周二，isoweekday() == 2
        task = make_task("weekly", weekday=2, hour=8, minute=0)
        self.assertTrue(is_due(task, datetime(2026, 10, 20, 8, 5)))

    def test_weekly_other_weekday(self):
        task = make_task("weekly", weekday=1, hour=8, minute=0)
        self.assertFalse(is_due(task, datetime(2026, 10, 20, 8, 5)))

    def test_monthly_matching_day(self):
        task = make_task("monthly", day_of_month=20, hour=8, minute=0)
        self.assertTrue(is_due(task, datetime(2026, 10, 20, 8, 0)))

    def test_monthly_other_day(self):
        task = make_task("monthly", day_of_month=15, hour=8, minute=0)
        self.assertFalse(is_due(task, datetime(2026, 10, 20, 8, 0)))

    def test_once_not_reached(self):
        task = make_task("once", run_at="2026-10-21 08:00")
        self.assertFalse(is_due(task, datetime(2026, 10, 20, 23, 59)))

    def test_once_reached(self):
        task = make_task("once", run_at="2026-10-20 08:00")
        self.assertTrue(is_due(task, datetime(2026, 10, 20, 8, 30)))

    def test_once_already_fired(self):
        task = make_task("once", run_at="2026-10-20 08:00")
        fired = datetime(2026, 10, 20, 8, 1)
        self.assertFalse(is_due(task, datetime(2026, 10, 20, 9, 0), last_fired=fired))


class SchedulerTickTest(unittest.TestCase):
    def setUp(self):
        self.store = TaskStore(Path("dataguard_tasks_test.json"))
        self.store.tasks = []

    def test_tick_fires_due_tasks_once_per_day(self):
        task = self.store.add(make_task("daily", hour=8, minute=0))
        scheduler = Scheduler(self.store)

        fired = scheduler.tick(datetime(2026, 10, 20, 8, 0))
        self.assertEqual(len(fired), 1)
        self.assertEqual(fired[0]["id"], task["id"])

        # 同一天再次 tick 不应重复触发
        again = scheduler.tick(datetime(2026, 10, 20, 8, 1))
        self.assertEqual(again, [])

    def test_tick_removes_once_task(self):
        once_task = self.store.add(make_task("once", run_at="2026-10-20 08:00"))
        scheduler = Scheduler(self.store)
        fired = scheduler.tick(datetime(2026, 10, 20, 8, 0))
        self.assertEqual(len(fired), 1)
        self.assertIsNone(self.store.get(once_task["id"]))


if __name__ == "__main__":
    unittest.main()

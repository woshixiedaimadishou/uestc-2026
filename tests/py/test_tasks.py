"""任务模型与持久化测试。"""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "python"))

from tasks import (  # noqa: E402
    TaskStore,
    default_task,
    describe_retention,
    describe_schedule,
    fill_defaults,
)


class TaskModelTest(unittest.TestCase):
    def test_default_task_has_required_sections(self):
        task = default_task()
        for key in ("id", "name", "source", "dest_dir", "filter", "options",
                    "schedule", "retention", "enabled"):
            self.assertIn(key, task)
        self.assertEqual(task["schedule"]["type"], "manual")

    def test_fill_defaults_completes_partial_task(self):
        partial = {"name": "demo", "schedule": {"type": "daily"}}
        filled = fill_defaults(partial)
        self.assertTrue(filled["id"])
        self.assertIn("filter", filled)
        self.assertIn("retention", filled)
        self.assertEqual(filled["schedule"]["hour"], 8)

    def test_describe_schedule(self):
        self.assertEqual(describe_schedule({"type": "manual"}), "手动执行")
        self.assertEqual(describe_schedule({"type": "daily", "hour": 8, "minute": 30}),
                         "每天 08:30")
        self.assertIn("每周", describe_schedule({"type": "weekly", "weekday": 3,
                                               "hour": 9, "minute": 0}))
        self.assertIn("每月", describe_schedule({"type": "monthly", "day_of_month": 15}))

    def test_describe_retention(self):
        self.assertEqual(describe_retention({}), "不淘汰")
        self.assertIn("3", describe_retention({"keep_count": 3}))
        self.assertIn("7", describe_retention({"keep_days": 7}))


class TaskStoreTest(unittest.TestCase):
    def setUp(self):
        self.tmp_dir = tempfile.TemporaryDirectory()
        self.path = Path(self.tmp_dir.name) / "tasks.json"
        self.store = TaskStore(self.path)

    def tearDown(self):
        self.tmp_dir.cleanup()

    def test_add_get_remove(self):
        task = self.store.add({"name": "t1", "source": "a", "dest_dir": "b"})
        self.assertTrue(task["id"])
        self.assertIsNotNone(self.store.get(task["id"]))

        self.assertTrue(self.store.remove(task["id"]))
        self.assertIsNone(self.store.get(task["id"]))
        self.assertFalse(self.store.remove("not-exist"))

    def test_update_and_toggle(self):
        task = self.store.add({"name": "t1"})
        task["name"] = "renamed"
        self.assertTrue(self.store.update(task))
        self.assertEqual(self.store.get(task["id"])["name"], "renamed")

        self.assertTrue(self.store.set_enabled(task["id"], False))
        self.assertFalse(self.store.get(task["id"])["enabled"])

    def test_save_load_roundtrip(self):
        self.store.add({"name": "persisted", "source": "src", "dest_dir": "dst"})
        self.assertTrue(self.store.save())

        other = TaskStore(self.path)
        self.assertTrue(other.load())
        self.assertEqual(len(other.tasks), 1)
        self.assertEqual(other.tasks[0]["name"], "persisted")

    def test_load_missing_file_is_empty(self):
        missing = TaskStore(Path(self.tmp_dir.name) / "missing.json")
        self.assertTrue(missing.load())
        self.assertEqual(missing.tasks, [])

    def test_load_corrupted_file_backs_up(self):
        self.path.write_text("{not valid json", encoding="utf-8")
        store = TaskStore(self.path)
        self.assertFalse(store.load())
        self.assertEqual(store.tasks, [])
        self.assertTrue(self.path.with_suffix(".bak").exists())


if __name__ == "__main__":
    unittest.main()

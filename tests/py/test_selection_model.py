"""还原条目选择逻辑测试（纯 Python，无需 Qt）。"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "python"))

from selection_model import SelectionTree, State  # noqa: E402

ENTRIES = [
    {"path": "a.txt", "size": "11"},
    {"path": "sub", "size": "0"},
    {"path": "sub/b.txt", "size": "5"},
    {"path": "sub/deep", "size": "0"},
    {"path": "sub/deep/c.bin", "size": "9"},
    {"path": "empty", "size": "0"},
]


def build():
    return SelectionTree(ENTRIES)


class BuildTreeTest(unittest.TestCase):
    def test_entry_count(self):
        self.assertEqual(build().entry_count, len(ENTRIES))

    def test_all_selected_initially(self):
        tree = build()
        self.assertTrue(tree.all_selected())
        self.assertEqual(tree.selected_count(), len(ENTRIES))

    def test_top_level_nodes(self):
        names = [node.name for node in build().top_level()]
        self.assertEqual(names, ["a.txt", "sub", "empty"])

    def test_compact_paths_for_full_selection(self):
        """全选时只输出顶层路径，而不是逐条列出。"""
        self.assertEqual(sorted(build().selected_paths()),
                         sorted(["a.txt", "sub", "empty"]))


class ToggleTest(unittest.TestCase):
    def test_uncheck_parent_cascades(self):
        tree = build()
        tree.set_checked(tree.node("sub"), False)
        self.assertEqual(sorted(tree.selected_paths()), ["a.txt", "empty"])
        self.assertEqual(tree.selected_count(), 2)

    def test_uncheck_child_makes_parent_partial(self):
        tree = build()
        tree.set_checked(tree.node("sub/deep/c.bin"), False)
        self.assertEqual(tree.node("sub").state(), State.PARTIAL)
        self.assertEqual(tree.node("sub/deep").state(), State.NONE)
        # 只输出仍被选中的边界条目（"sub" 变为半选，需向下展开）
        self.assertEqual(sorted(tree.selected_paths()),
                         sorted(["a.txt", "sub/b.txt", "empty"]))

    def test_check_back_restores_full_state(self):
        tree = build()
        tree.set_checked(tree.node("sub/deep/c.bin"), False)
        tree.set_checked(tree.node("sub/deep/c.bin"), True)
        self.assertEqual(tree.node("sub").state(), State.FULL)
        self.assertEqual(sorted(tree.selected_paths()), sorted(["a.txt", "sub", "empty"]))

    def test_none_selected(self):
        tree = build()
        tree.set_all(False)
        self.assertTrue(tree.none_selected())
        self.assertEqual(tree.selected_paths(), [])
        self.assertEqual(tree.selected_count(), 0)

    def test_select_again_after_none(self):
        tree = build()
        tree.set_all(False)
        tree.set_checked(tree.node("sub/deep"), True)
        self.assertEqual(tree.selected_paths(), ["sub/deep"])
        self.assertFalse(tree.all_selected())


class VirtualNodeTest(unittest.TestCase):
    """档案里可能没有中间目录条目，虚拟节点不能出现在清单里。"""

    def test_virtual_directories_are_expanded(self):
        tree = SelectionTree([{"path": "only/deep/file.txt", "size": "1"}])
        self.assertEqual(tree.selected_paths(), ["only/deep/file.txt"])

    def test_real_directory_prefix_is_kept(self):
        tree = SelectionTree([
            {"path": "dir", "size": "0"},
            {"path": "dir/x.txt", "size": "1"},
            {"path": "dir/y.txt", "size": "2"},
        ])
        self.assertEqual(tree.selected_paths(), ["dir"])


class StateTest(unittest.TestCase):
    def test_partial_parent_click_is_derived(self):
        tree = build()
        tree.set_checked(tree.node("sub/b.txt"), False)
        self.assertNotEqual(tree.node("sub").state(), State.FULL)
        self.assertNotEqual(tree.node("sub").state(), State.NONE)


if __name__ == "__main__":
    unittest.main()

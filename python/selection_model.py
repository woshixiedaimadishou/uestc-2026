"""还原条目选择的纯逻辑模型（不依赖 Qt，便于单元测试）。

勾选规则
--------
1. 勾选/取消父节点 → 整棵子树随之勾选/取消；
2. 子树全选中 → 父节点为选中；部分选中 → 父节点为半选（``PARTIAL``）；
3. 生成还原清单时做「压缩」：完全选中的子树只输出其根路径（C++ 侧按目录前缀
   还原整棵子树），部分选中的子树继续向下拆，保证清单尽量短。

这样即使档案里有上万个条目，传给 CLI 的清单也只有「选中的目录数 + 边界文件数」。
"""

from __future__ import annotations

from dataclasses import dataclass, field


class State:
    """节点勾选状态（取值为 Qt.CheckState 的数值，便于直接对接控件）。"""

    NONE = 0
    PARTIAL = 1
    FULL = 2


@dataclass
class EntryNode:
    """档案条目的树节点。

    ``key`` 为该节点在档案内的相对路径（用 ``/`` 分隔）；
    ``is_entry`` 表示档案里是否真有这一条（中间目录可能是补出来的虚拟节点）。
    """

    key: str
    name: str
    is_entry: bool = False
    size: str = ""
    parent: "EntryNode | None" = None
    children: list["EntryNode"] = field(default_factory=list)
    checked: bool = True

    def add_child(self, node: "EntryNode") -> "EntryNode":
        node.parent = self
        node.checked = self.checked
        self.children.append(node)
        return node

    def state(self) -> int:
        """由自身与子节点推导出显示状态。"""
        if not self.children:
            return State.FULL if self.checked else State.NONE
        states = [child.state() for child in self.children]
        if all(item == State.FULL for item in states):
            return State.FULL
        if all(item == State.NONE for item in states):
            return State.NONE
        return State.PARTIAL

    def walk(self):
        yield self
        for child in self.children:
            yield from child.walk()


class SelectionTree:
    """把档案条目列表组织成可勾选的树。"""

    def __init__(self, entries: list[dict] | None = None):
        self.root = EntryNode(key="", name="")
        self._nodes: dict[str, EntryNode] = {}
        self.entry_count = 0
        for entry in entries or []:
            self.add(entry.get("path", ""), entry.get("size", ""))

    # ---------- 构建 ----------
    def add(self, path: str, size: str = "") -> EntryNode | None:
        parts = [part for part in str(path).split("/") if part]
        if not parts:
            return None

        node = self.root
        for index, part in enumerate(parts):
            key = "/".join(parts[:index + 1])
            child = self._nodes.get(key)
            if child is None:
                child = node.add_child(EntryNode(key=key, name=part))
                self._nodes[key] = child
            node = child

        node.is_entry = True
        node.size = str(size)
        self.entry_count += 1
        return node

    def node(self, key: str) -> EntryNode | None:
        return self._nodes.get(key)

    def top_level(self) -> list[EntryNode]:
        return self.root.children

    # ---------- 勾选 ----------
    def set_checked(self, node: EntryNode, checked: bool) -> None:
        """设置节点及其整棵子树。"""
        self._apply(node, checked)

    def set_all(self, checked: bool) -> None:
        for child in self.root.children:
            self._apply(child, checked)

    def _apply(self, node: EntryNode, checked: bool) -> None:
        node.checked = checked
        for child in node.children:
            self._apply(child, checked)

    # ---------- 查询 ----------
    def selected_count(self) -> int:
        """已勾选的真实条目数。"""
        return sum(1 for node in self._nodes.values() if node.is_entry and node.state() == State.FULL)

    def all_selected(self) -> bool:
        return self.entry_count > 0 and self.selected_count() == self.entry_count

    def none_selected(self) -> bool:
        return self.selected_count() == 0

    def selected_paths(self) -> list[str]:
        """生成还原清单：完全选中的子树只输出根路径。"""
        out: list[str] = []
        for child in self.root.children:
            self._collect(child, out)
        return out

    def _collect(self, node: EntryNode, out: list[str]) -> None:
        state = node.state()
        if state == State.NONE:
            return
        if state == State.FULL:
            if node.is_entry:
                out.append(node.key)
                return
            # 虚拟中间目录：继续向下拆，避免清单里出现档案中不存在的路径
            for child in node.children:
                self._collect(child, out)
            return
        for child in node.children:
            self._collect(child, out)

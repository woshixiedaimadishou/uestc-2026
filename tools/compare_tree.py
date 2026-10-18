#!/usr/bin/env python3
"""递归比较两个目录树：文件内容逐字节比对 + 目录结构比对。

用法: python compare_tree.py <源目录> <还原目录>
退出码: 0=一致, 1=存在差异, 2=参数/路径错误
"""
import sys
from pathlib import Path


def compare(src: Path, dst: Path) -> list[str]:
    errors: list[str] = []

    src_files = {p.relative_to(src) for p in src.rglob("*") if p.is_file()}
    dst_files = {p.relative_to(dst) for p in dst.rglob("*") if p.is_file()}

    for rel in sorted(src_files - dst_files):
        errors.append(f"缺文件: {rel}")
    for rel in sorted(dst_files - src_files):
        errors.append(f"多出文件: {rel}")

    for rel in sorted(src_files & dst_files):
        if (src / rel).read_bytes() != (dst / rel).read_bytes():
            errors.append(f"内容不一致: {rel}")

    src_dirs = {p.relative_to(src) for p in src.rglob("*") if p.is_dir()}
    dst_dirs = {p.relative_to(dst) for p in dst.rglob("*") if p.is_dir()}
    for rel in sorted(src_dirs - dst_dirs):
        errors.append(f"缺目录: {rel}")

    return errors


def main() -> int:
    if len(sys.argv) != 3:
        print("用法: compare_tree.py <源目录> <还原目录>")
        return 2

    src = Path(sys.argv[1])
    dst = Path(sys.argv[2])
    if not src.is_dir():
        print(f"源目录不存在: {src}")
        return 2
    if not dst.is_dir():
        print(f"还原目录不存在: {dst}")
        return 2

    errors = compare(src, dst)
    if errors:
        print(f"目录树不一致，共 {len(errors)} 处差异：")
        for item in errors[:20]:
            print(f"  - {item}")
        return 1

    total = len(list(src.rglob("*")))
    print(f"目录树一致：共 {total} 个条目")
    return 0


if __name__ == "__main__":
    sys.exit(main())

"""生成交付用的压缩包：

1. 发布包（GitHub Release 用）：release/DataGuard-<ver>-win64.zip
   内容只有一个 DataGuard/ 目录，解压双击 DataGuard.exe 即可使用。

2. 课程提交包（学校平台用）：
   release/DataGuard-<ver>-提交包-含可执行程序.zip   源码 + dist/DataGuard
   release/DataGuard-<ver>-提交包-仅源码.zip         源码（体积小，适合有大小限制的平台）

用法：
    python tools/package_release.py                 # 两个提交包 + 发布包
    python tools/package_release.py --version 1.0.0
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / "dist" / "DataGuard"

# 提交包里排除的内容（构建产物、本地配置、个人环境）
EXCLUDE_DIRS = {
    "build", "release", ".git", ".vscode", ".workbuddy", "__pycache__", ".idea",
}
EXCLUDE_PREFIXES = ("dist/DataGuard_old",)
EXCLUDE_SUFFIXES = (".pyc", ".pyo", ".spec")
EXCLUDE_FILES = {
    "dataguard_tasks.json",        # 本机任务数据
    "dataguard_tasks_test.json",
    "dataguard_settings.json",     # 本机设置
}

README_TXT = """DataGuard 数据备份软件 {version}
================================

解压后双击 DataGuard.exe 即可运行，无需安装 Python 或编译器。
详细说明见同目录下的「使用说明.txt」。

源码与构建脚本见 GitHub 仓库：
https://github.com/woshixiedaimadishou/uestc-2026
"""


def should_skip(relative: Path) -> bool:
    parts = relative.parts
    if any(part in EXCLUDE_DIRS for part in parts):
        return True
    posix = relative.as_posix()
    if any(posix.startswith(prefix) for prefix in EXCLUDE_PREFIXES):
        return True
    if any(part.endswith(EXCLUDE_SUFFIXES) for part in parts):
        return True
    return posix in EXCLUDE_FILES


def iter_files(base: Path, extra_skip_top: tuple[str, ...] = ()):
    for path in sorted(base.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(base)
        if relative.parts and relative.parts[0] in extra_skip_top:
            continue
        if should_skip(relative):
            continue
        yield path, relative


def add_dir(zf: zipfile.ZipFile, base: Path, arc_prefix: str,
            extra_skip_top: tuple[str, ...] = ()) -> int:
    count = 0
    for path, relative in iter_files(base, extra_skip_top):
        zf.write(path, (Path(arc_prefix) / relative).as_posix())
        count += 1
    return count


def build_portable(version: str) -> Path:
    if not DIST.is_dir():
        raise SystemExit(f"找不到 {DIST}，请先执行 make dist")

    target = ROOT / "release" / f"DataGuard-{version}-win64.zip"
    target.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
        count = add_dir(zf, DIST, "DataGuard")
        zf.writestr("DataGuard/README.txt", README_TXT.format(version=version))
    print(f"[OK] 发布包：{target}  （{count + 1} 个文件，{target.stat().st_size / 1048576:.1f} MB）")
    return target


def build_submission(version: str, include_dist: bool) -> Path:
    suffix = "提交包-含可执行程序" if include_dist else "提交包-仅源码"
    target = ROOT / "release" / f"DataGuard-{version}-{suffix}.zip"
    target.parent.mkdir(parents=True, exist_ok=True)

    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
        if include_dist:
            # dist 单独加一次，根目录遍历时跳过，避免同名文件写两遍
            count = add_dir(zf, ROOT, "", ("dist",))
            count += add_dir(zf, DIST, "dist/DataGuard")
        else:
            count = add_dir(zf, ROOT, "", ("dist",))

    print(f"[OK] {suffix}：{target}  （{count} 个文件，{target.stat().st_size / 1048576:.1f} MB）")
    return target


def git_archive(version: str) -> None:
    """额外产出 git 原生的源码压缩包（与 tag 完全一致）。"""
    target = ROOT / "release" / f"DataGuard-{version}-git-archive.zip"
    target.parent.mkdir(parents=True, exist_ok=True)
    try:
        subprocess.run(["git", "archive", "--format=zip", f"--output={target}", "HEAD"],
                       cwd=ROOT, check=True)
        print(f"[OK] 源码快照：{target}  （{target.stat().st_size / 1048576:.1f} MB）")
    except (subprocess.CalledProcessError, FileNotFoundError):
        print("[跳过] 无法执行 git archive（不影响其他压缩包）")


def main() -> int:
    parser = argparse.ArgumentParser(description="生成发布包与课程提交包")
    parser.add_argument("--version", default="1.0.0")
    parser.add_argument("--skip-archive", action="store_true", help="不生成 git archive 快照")
    args = parser.parse_args()

    build_portable(args.version)
    build_submission(args.version, include_dist=True)
    build_submission(args.version, include_dist=False)
    if not args.skip_archive:
        git_archive(args.version)
    print("\n全部产物位于 release/ 目录")
    return 0


if __name__ == "__main__":
    sys.exit(main())

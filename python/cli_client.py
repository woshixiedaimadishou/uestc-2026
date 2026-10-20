"""DataGuard 命令行客户端封装。

职责：
1. 探测 dataguard-cli.exe 的位置（打包/开发两种环境）；
2. 组装命令行参数（纯函数，便于单元测试）；
3. 以 subprocess 调用 CLI，并逐行解析 stdout 行协议
   （PROGRESS / PROGRESS_B / LOG / DONE）。
"""

from __future__ import annotations

import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Optional

# 行协议标记
PROGRESS = "progress"
LOG = "log"
DONE = "done"

ProgressCallback = Callable[[int, int, str], None]
LogCallback = Callable[[str, str], None]


def parse_line(line: str):
    """解析一行行协议输出。

    返回 ``(kind, payload)``：
      - ("progress", (index, total, path))
      - ("log", (level, message))
      - ("done", (success, failed, archive))
    无法识别时返回 None。
    """
    text = line.rstrip("\r\n")
    if not text:
        return None

    parts = text.split("\t")
    tag = parts[0]

    if tag == "PROGRESS" and len(parts) >= 4:
        try:
            return PROGRESS, (int(parts[1]), int(parts[2]), parts[3])
        except ValueError:
            return None

    if tag == "PROGRESS_B" and len(parts) >= 4:
        try:
            return PROGRESS, (int(parts[1]), int(parts[2]), "")
        except ValueError:
            return None

    if tag == "LOG" and len(parts) >= 3:
        return LOG, (parts[1], "\t".join(parts[2:]))

    if tag == "DONE" and len(parts) >= 4:
        try:
            return DONE, (int(parts[1]), int(parts[2]), parts[3])
        except ValueError:
            return None

    if tag in ("OK", "FAIL"):
        return LOG, ("INFO", text)

    return None


def detect_cli_path() -> Optional[Path]:
    """按优先级探测 dataguard-cli.exe。"""
    env_path = os.environ.get("DATAGUARD_CLI")
    if env_path:
        candidate = Path(env_path)
        if candidate.is_file():
            return candidate

    # PyInstaller 打包环境：exe 同目录
    if getattr(sys, "frozen", False):
        candidate = Path(sys.executable).resolve().parent / "dataguard-cli.exe"
        if candidate.is_file():
            return candidate

    here = Path(__file__).resolve().parent
    candidates = [
        here.parent / "build" / "dataguard-cli.exe",
        here.parent / "dist" / "DataGuard" / "dataguard-cli.exe",
        Path.cwd() / "dataguard-cli.exe",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return None


def build_backup_args(cli_path,
                      source,
                      dest,
                      name: str = "backup",
                      compress: bool = False,
                      encrypt: bool = False,
                      password: str = "",
                      filters: Optional[dict] = None) -> list[str]:
    """组装 backup 命令参数。"""
    args = [str(cli_path), "backup", str(source), str(dest), "--name", name or "backup"]
    if compress:
        args.append("--compress")
    if encrypt:
        args.extend(["--encrypt", "--password", password])

    filters = filters or {}
    for ext in filters.get("include_exts", []):
        args.extend(["--include-ext", ext])
    for ext in filters.get("exclude_exts", []):
        args.extend(["--exclude-ext", ext])
    for glob in filters.get("include_path_globs", []):
        args.extend(["--include-glob", glob])
    for glob in filters.get("exclude_path_globs", []):
        args.extend(["--exclude-glob", glob])
    for owner in filters.get("include_owners", []):
        args.extend(["--owner", owner])

    min_size = filters.get("min_size", -1)
    if isinstance(min_size, int) and min_size >= 0:
        args.extend(["--min-size", str(min_size)])
    max_size = filters.get("max_size", -1)
    if isinstance(max_size, int) and max_size >= 0:
        args.extend(["--max-size", str(max_size)])

    modified_after = filters.get("modified_after", "")
    if modified_after:
        args.extend(["--modified-after", str(modified_after)])
    if filters.get("follow_symlinks"):
        args.append("--follow-symlinks")

    return args


def build_restore_args(cli_path,
                       archive,
                       dest,
                       password: str = "",
                       overwrite: bool = True,
                       files: Optional[list[str]] = None) -> list[str]:
    """组装 restore 命令参数。"""
    args = [str(cli_path), "restore", str(archive), str(dest)]
    if password:
        args.extend(["--password", password])
    if not overwrite:
        args.append("--no-overwrite")
    if files:
        args.extend(["--files", ",".join(files)])
    return args


def build_retention_args(cli_path,
                         dest_dir,
                         task_name: str,
                         keep_count: int = 0,
                         keep_days: int = 0) -> list[str]:
    """组装 retention（数据淘汰）命令参数。"""
    args = [str(cli_path), "retention", str(dest_dir), task_name]
    if keep_count > 0:
        args.extend(["--keep-count", str(keep_count)])
    if keep_days > 0:
        args.extend(["--keep-days", str(keep_days)])
    return args


def build_list_args(cli_path, archive, password: str = "") -> list[str]:
    args = [str(cli_path), "list", str(archive)]
    if password:
        args.extend(["--password", password])
    return args


@dataclass
class CliResult:
    ok: bool = False
    success: int = 0
    failed: int = 0
    archive: str = ""
    message: str = ""


class CliClient:
    """dataguard-cli.exe 的 Python 封装。"""

    def __init__(self, cli_path=None):
        self.cli_path = Path(cli_path) if cli_path else detect_cli_path()

    def available(self) -> bool:
        return self.cli_path is not None and Path(self.cli_path).is_file()

    def _popen(self, args):
        creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0
        return subprocess.Popen(
            args,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
            creationflags=creationflags,
        )

    def _run(self, args,
             on_progress: Optional[ProgressCallback] = None,
             on_log: Optional[LogCallback] = None) -> CliResult:
        if not self.available():
            return CliResult(False, message="未找到 dataguard-cli.exe，请设置环境变量 DATAGUARD_CLI")

        try:
            process = self._popen(args)
        except OSError as exc:
            return CliResult(False, message=f"启动命令行程序失败: {exc}")

        result = CliResult(False)
        stderr_text = ""

        if process.stdout is not None:
            for line in process.stdout:
                parsed = parse_line(line)
                if parsed is None:
                    continue
                kind, payload = parsed
                if kind == PROGRESS and on_progress:
                    on_progress(*payload)
                elif kind == LOG and on_log:
                    on_log(*payload)
                elif kind == DONE:
                    result.success, result.failed, result.archive = payload
                    result.ok = True

        if process.stderr is not None:
            stderr_text = process.stderr.read() or ""
        process.wait()

        if process.returncode != 0:
            result.ok = False
            result.message = stderr_text.strip() or "命令执行失败"
        return result

    def backup(self, source, dest, **kwargs) -> CliResult:
        on_progress = kwargs.pop("on_progress", None)
        on_log = kwargs.pop("on_log", None)
        args = build_backup_args(self.cli_path, source, dest, **kwargs)
        return self._run(args, on_progress, on_log)

    def restore(self, archive, dest, **kwargs) -> CliResult:
        on_progress = kwargs.pop("on_progress", None)
        on_log = kwargs.pop("on_log", None)
        args = build_restore_args(self.cli_path, archive, dest, **kwargs)
        return self._run(args, on_progress, on_log)

    def retention(self, dest_dir, task_name, keep_count=0, keep_days=0) -> CliResult:
        args = build_retention_args(self.cli_path, dest_dir, task_name, keep_count, keep_days)
        return self._run(args)

    def verify(self, archive, password="") -> CliResult:
        args = [str(self.cli_path), "verify", str(archive)]
        if password:
            args.extend(["--password", password])
        return self._run(args)

    def list_archive(self, archive, password="") -> list[dict]:
        """调用 list 命令并解析条目列表。"""
        if not self.available():
            return []
        try:
            process = self._popen(build_list_args(self.cli_path, archive, password))
        except OSError:
            return []

        entries: list[dict] = []
        if process.stdout is not None:
            for line in process.stdout:
                text = line.rstrip("\r\n")
                if not text or text.startswith("类型") or text.startswith("共 "):
                    continue
                parts = text.split("\t")
                if len(parts) < 4:
                    continue
                entries.append({
                    "kind": parts[0],
                    "size": parts[1],
                    "mtime": parts[2],
                    "path": "\t".join(parts[3:]),
                })
        process.wait()
        return entries

    def version(self) -> str:
        if not self.available():
            return ""
        try:
            process = self._popen([str(self.cli_path), "--version"])
            output = process.stdout.read() if process.stdout else ""
            process.wait()
            return output.strip()
        except OSError:
            return ""

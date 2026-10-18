#!/usr/bin/env bash
# 集成测试：备份 → 列举核对 → 完整性校验 → 还原 → 目录树比对
# 用法: bash tests/integration/backup_roundtrip.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CLI="$ROOT/build/dataguard-cli.exe"
PYTHON="${PYTHON:-E:/anaconda3/python.exe}"
WORK="${TMPDIR_BASE:-C:/Users/Public}/dg_integration_$$"

cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

rm -rf "$WORK"
mkdir -p "$WORK/src/sub/deep" "$WORK/src/empty"
printf 'hello world\n' > "$WORK/src/a.txt"
printf 'int main() { return 0; }\n' > "$WORK/src/sub/b.cpp"
printf 'deep file\n' > "$WORK/src/sub/deep/c.dat"

echo "[1/5] 备份"
"$CLI" backup "$WORK/src" "$WORK/out" --name itest
ARCHIVE=$(ls "$WORK"/out/*.bkpk | head -1)
echo "  档案: $ARCHIVE"

echo "[2/5] 列举核对"
"$CLI" list "$ARCHIVE" | grep -q "a.txt"
"$CLI" list "$ARCHIVE" | grep -q "sub/deep/c.dat"
"$CLI" list "$ARCHIVE" | grep -q "empty"

echo "[3/5] 完整性校验"
"$CLI" verify "$ARCHIVE"

echo "[4/5] 还原"
"$CLI" restore "$ARCHIVE" "$WORK/restored"

echo "[5/5] 目录树比对"
"$PYTHON" "$ROOT/tools/compare_tree.py" "$WORK/src" "$WORK/restored"

echo "INTEGRATION OK"

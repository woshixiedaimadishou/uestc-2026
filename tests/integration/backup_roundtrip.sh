#!/usr/bin/env bash
# 集成测试：备份 → 列举核对 → 完整性校验 → 还原 → 目录树比对
# 覆盖场景：无压缩 / DEFLATE 压缩
# 用法: bash tests/integration/backup_roundtrip.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CLI="$ROOT/build/dataguard-cli.exe"
PYTHON="${PYTHON:-E:/anaconda3/python.exe}"
WORK="${TMPDIR_BASE:-C:/Users/Public}/dg_integration_$$"

cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

# ---------- 构造测试数据集 ----------
rm -rf "$WORK"
mkdir -p "$WORK/src/sub/deep" "$WORK/src/empty"
printf 'hello world\n' > "$WORK/src/a.txt"
printf 'int main() { return 0; }\n' > "$WORK/src/sub/b.cpp"
printf 'deep file\n' > "$WORK/src/sub/deep/c.dat"
# 可压缩的大文本（用于校验压缩率）
"$PYTHON" -c "
import sys
text = ('数据备份软件需要保证数据的完整性与可恢复性。\n' * 200)
open(sys.argv[1], 'w', encoding='utf-8').write(text)
" "$WORK/src/text.txt"

# ---------- 场景执行 ----------
run_case() {
    local label="$1"
    local extra="$2"
    local dir="$WORK/$label"

    echo ""
    echo "========== 场景: $label =========="

    echo "[1/5] 备份"
    if [ -n "$extra" ]; then
        "$CLI" backup "$WORK/src" "$dir/archives" --name "$label" "$extra"
    else
        "$CLI" backup "$WORK/src" "$dir/archives" --name "$label"
    fi
    local archive
    archive=$(ls "$dir"/archives/*.bkpk | head -1)
    echo "  档案: $archive"

    echo "[2/5] 列举核对"
    "$CLI" list "$archive" | grep -q "a.txt"
    "$CLI" list "$archive" | grep -q "sub/deep/c.dat"
    "$CLI" list "$archive" | grep -q "empty"
    "$CLI" list "$archive" | grep -q "text.txt"

    echo "[3/5] 完整性校验"
    "$CLI" verify "$archive"

    echo "[4/5] 还原"
    "$CLI" restore "$archive" "$dir/restored"

    echo "[5/5] 目录树比对"
    "$PYTHON" "$ROOT/tools/compare_tree.py" "$WORK/src" "$dir/restored"

    echo "  场景 $label 通过"
}

run_case plain ""
run_case compressed "--compress"

# ---------- 压缩率断言 ----------
PLAIN_SIZE=$(stat -c %s "$WORK"/plain/archives/*.bkpk)
COMPRESSED_SIZE=$(stat -c %s "$WORK"/compressed/archives/*.bkpk)
echo ""
echo "压缩率断言：未压缩=$PLAIN_SIZE 字节，压缩后=$COMPRESSED_SIZE 字节"
if [ "$COMPRESSED_SIZE" -ge "$PLAIN_SIZE" ]; then
    echo "错误：压缩档案未变小"
    exit 1
fi

echo ""
echo "INTEGRATION OK"

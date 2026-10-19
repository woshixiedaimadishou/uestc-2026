#!/usr/bin/env bash
# 集成测试：备份 → 列举核对 → 完整性校验 → 还原 → 目录树比对
# 覆盖 4 组组合矩阵：无 / 压缩 / 加密 / 压缩+加密
# 用法: bash tests/integration/backup_roundtrip.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CLI="$ROOT/build/dataguard-cli.exe"
PYTHON="${PYTHON:-E:/anaconda3/python.exe}"
WORK="${TMPDIR_BASE:-C:/Users/Public}/dg_integration_$$"
PASSWORD="IntegTest#2026"

cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

# ---------- 构造测试数据集 ----------
rm -rf "$WORK"
mkdir -p "$WORK/src/sub/deep" "$WORK/src/empty"
printf 'hello world\n' > "$WORK/src/a.txt"
printf 'int main() { return 0; }\n' > "$WORK/src/sub/b.cpp"
printf 'deep file\n' > "$WORK/src/sub/deep/c.dat"
"$PYTHON" -c "
import sys
text = ('数据备份软件需要保证数据的完整性与可恢复性。\n' * 200)
open(sys.argv[1], 'w', encoding='utf-8').write(text)
" "$WORK/src/text.txt"

# ---------- 场景执行 ----------
run_case() {
    local label="$1"
    local options="$2"
    local password="$3"
    local dir="$WORK/$label"

    echo ""
    echo "========== 场景: $label =========="

    echo "[1/5] 备份"
    if [ -n "$password" ]; then
        # shellcheck disable=SC2086
        "$CLI" backup "$WORK/src" "$dir/archives" --name "$label" $options --password "$password"
    else
        # shellcheck disable=SC2086
        "$CLI" backup "$WORK/src" "$dir/archives" --name "$label" $options
    fi
    local archive
    archive=$(ls "$dir"/archives/*.bkpk | head -1)
    echo "  档案: $archive"

    echo "[2/5] 列举核对"
    if [ -n "$password" ]; then
        "$CLI" list "$archive" --password "$password" | grep -q "a.txt"
        "$CLI" list "$archive" --password "$password" | grep -q "sub/deep/c.dat"
        "$CLI" list "$archive" --password "$password" | grep -q "empty"
    else
        "$CLI" list "$archive" | grep -q "a.txt"
        "$CLI" list "$archive" | grep -q "sub/deep/c.dat"
        "$CLI" list "$archive" | grep -q "empty"
    fi

    echo "[3/5] 完整性校验"
    if [ -n "$password" ]; then
        "$CLI" verify "$archive" --password "$password"
    else
        "$CLI" verify "$archive"
    fi

    echo "[4/5] 还原"
    if [ -n "$password" ]; then
        "$CLI" restore "$archive" "$dir/restored" --password "$password"
    else
        "$CLI" restore "$archive" "$dir/restored"
    fi

    echo "[5/5] 目录树比对"
    "$PYTHON" "$ROOT/tools/compare_tree.py" "$WORK/src" "$dir/restored"

    echo "  场景 $label 通过"
}

run_case plain "" ""
run_case compressed "--compress" ""
run_case encrypted "--encrypt" "$PASSWORD"
run_case encrypted_compressed "--compress --encrypt" "$PASSWORD"

# ---------- 组合断言 ----------
PLAIN_SIZE=$(stat -c %s "$WORK"/plain/archives/*.bkpk)
ZIP_SIZE=$(stat -c %s "$WORK"/compressed/archives/*.bkpk)

echo ""
echo "体积断言：plain=$PLAIN_SIZE compressed=$ZIP_SIZE"
if [ "$ZIP_SIZE" -ge "$PLAIN_SIZE" ]; then
    echo "错误：压缩档案未变小"
    exit 1
fi

# 加密档案不应包含明文片段
if grep -q "hello world" "$WORK"/encrypted/archives/*.bkpk 2>/dev/null; then
    echo "错误：加密档案中出现明文"
    exit 1
fi
echo "加密档案明文检查通过"

# 错误密码必须被拒绝
echo ""
echo "错误密码拒绝测试"
if "$CLI" restore "$WORK"/encrypted/archives/*.bkpk "$WORK/bad_restore" --password "wrong-password" >/dev/null 2>&1; then
    echo "错误：错误密码竟然还原成功"
    exit 1
fi
echo "错误密码已被拒绝"

echo ""
echo "INTEGRATION OK"

"""把 deflate_probe 的输出画成一页可读的 HTML（离线、单文件、无外部依赖）。

用法：
    python tools/deflate_visualize.py                       # 用内置示例
    python tools/deflate_visualize.py --text "aaaaaa"       # 指定文本（ASCII）
    python tools/deflate_visualize.py --file some.txt       # 指定文件（支持中文/二进制）
    python tools/deflate_visualize.py --out build/x.html    # 指定输出

页面上会同时做三项交叉验证：
  1. 位流重新装回字节是否等于压缩输出（自洽）
  2. Python 标准库 zlib（raw deflate）能否解开我们生成的位流
  3. dataguard-cli 真实备份 → 还原，文件内容是否一致，以及压缩/未压缩档案体积
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROBE = ROOT / "build" / "deflate_probe.exe"
CLI = ROOT / "build" / "dataguard-cli.exe"

DEFAULT_TEXT = "The quick brown fox. " * 8


KIND_COLORS = {
    "header": "#CECBF6",
    "lit": "#B5D4F4",
    "lencode": "#FAC775",
    "lenextra": "#F7E0B5",
    "distcode": "#F5C4B3",
    "distextra": "#F7D4C8",
    "end": "#9FE1CB",
    "pad": "#E2E0D8",
}

KIND_NAMES = {
    "header": "块头",
    "lit": "字面",
    "lencode": "长度码",
    "lenextra": "长度额外位",
    "distcode": "距离码",
    "distextra": "距离额外位",
    "end": "结束符",
    "pad": "补齐",
}


def run_probe(text: str | None, file_path: str | None) -> dict:
    if not PROBE.is_file():
        raise SystemExit(f"找不到探针程序：{PROBE}\n请先执行：make probe")
    args = [str(PROBE)]
    if file_path:
        args += ["--file", file_path]
    else:
        args += ["--text", text or DEFAULT_TEXT]
    out = subprocess.run(args, capture_output=True, text=True, encoding="utf-8")
    if out.returncode != 0:
        raise SystemExit(f"探针执行失败：{out.stderr.strip()}")
    return json.loads(out.stdout)


def zlib_check(packed: bytes, original: bytes) -> tuple[bool, str]:
    """用标准库 zlib 以 raw deflate（wbits=-15）解我们的位流。"""
    try:
        restored = zlib.decompress(packed, -15)
    except zlib.error as exc:
        return False, f"zlib 解压失败：{exc}"
    if restored != original:
        return False, "zlib 解压成功但内容不一致"
    return True, "zlib（raw deflate，wbits=-15）解压后与原文完全一致"


def cli_roundtrip(payload: bytes) -> dict | None:
    """用真实 CLI 备份两次（不压缩 / 压缩），再还原比对。"""
    if not CLI.is_file():
        return None
    work = Path(tempfile.mkdtemp(prefix="dg_demo_"))
    try:
        src = work / "src"
        src.mkdir()
        (src / "sample.txt").write_bytes(payload)

        sizes = {}
        for label, extra in (("plain", []), ("compressed", ["--compress"])):
            out_dir = work / label
            subprocess.run([str(CLI), "backup", str(src), str(out_dir), "--name", label, *extra],
                           check=True, capture_output=True)
            archive = next(out_dir.glob("*.bkpk"))
            sizes[label] = archive.stat().st_size
            if label == "compressed":
                subprocess.run([str(CLI), "restore", str(archive), str(work / "restored")],
                               check=True, capture_output=True)
                restored = (work / "restored" / "sample.txt").read_bytes()
                sizes["restore_ok"] = restored == payload
        return sizes
    except (subprocess.CalledProcessError, StopIteration) as exc:
        return {"error": str(exc)}
    finally:
        shutil.rmtree(work, ignore_errors=True)


def stream_bits(packed: bytes, bit_count: int) -> list[int]:
    """按写盘顺序还原位流：第 k 位来自第 k//8 个字节的第 k%8 位（LSB 优先）。"""
    bits = []
    for index in range(bit_count):
        byte = packed[index // 8]
        bits.append((byte >> (index % 8)) & 1)
    return bits


def render_bit_grid(bits: list[int], segments: list[dict], limit: int) -> tuple[str, str]:
    kinds = []
    for seg in segments:
        kinds.extend([seg["kind"]] * seg["bits"])

    cells = []
    for index, bit in enumerate(bits[:limit]):
        kind = kinds[index] if index < len(kinds) else "pad"
        label = ""
        cursor = 0
        for seg in segments:
            if cursor <= index < cursor + seg["bits"]:
                label = seg["label"]
                break
            cursor += seg["bits"]
        edge = "border-left:3px solid #2C2C2A;" if index and index % 8 == 0 else ""
        cells.append(
            f'<span class="bit" title="第 {index + 1} 位 · {label}" '
            f'style="background:{KIND_COLORS.get(kind, "#eee")};{edge}">{bit}</span>'
        )

    grid = "".join(cells)
    if len(bits) > limit:
        grid += f'<span class="more">… 后面还有 {len(bits) - limit} 位未显示</span>'

    legend = "".join(
        f'<span class="lg"><i style="background:{KIND_COLORS[kind]}"></i>{name}</span>'
        for kind, name in KIND_NAMES.items()
    )
    return grid, legend


def render_tokens(tokens: list[dict]) -> str:
    cards = []
    for index, token in enumerate(tokens):
        if token["kind"] == "lit":
            body = f'<b>{token["char"] or "·"}</b><em>{token["byte"]}</em>'
            cls = "tk lit"
        else:
            body = (f'<b>回退 {token["distance"]}</b><em>抄 {token["length"]} 字节</em>')
            cls = "tk match"
        cards.append(f'<div class="{cls}"><span class="idx">{index + 1}</span>{body}</div>')
    return "".join(cards)


def render_table(tokens: list[dict], segments: list[dict], payload: bytes) -> str:
    # 每个 token 占用的位数 = 归属它的所有段之和
    per_token: dict[int, list[dict]] = {}
    for seg in segments:
        if seg["token"] >= 0:
            per_token.setdefault(seg["token"], []).append(seg)

    rows = []
    for index, token in enumerate(tokens):
        parts = per_token.get(index, [])
        bits = sum(part["bits"] for part in parts)
        detail = " + ".join(f'{KIND_NAMES.get(p["kind"], p["kind"])} {p["bits"]}' for p in parts)
        if token["kind"] == "lit":
            text = f'字面 {token["char"] or "?"}（{token["byte"]}）'
        else:
            text = f'匹配：回退 {token["distance"]}，长度 {token["length"]}'
        snippet = payload[token["offset"]:token["offset"] + (1 if token["kind"] == "lit" else token["length"])]
        snippet_text = snippet.decode("utf-8", "replace").replace("&", "&amp;").replace("<", "&lt;")
        rows.append(
            f'<tr><td>{index + 1}</td><td>{text}</td>'
            f'<td class="mono">{snippet_text[:40]}</td>'
            f'<td>{bits}</td><td class="small">{detail}</td></tr>'
        )
    return "".join(rows)


CSS = """
:root { color-scheme: light; }
* { box-sizing: border-box; }
body { margin: 0; padding: 32px 24px 64px; background: #FAF9F5; color: #2C2C2A;
  font-family: -apple-system, "Segoe UI", "Microsoft YaHei", system-ui, sans-serif;
  line-height: 1.65; }
.wrap { max-width: 960px; margin: 0 auto; }
h1 { font-size: 22px; font-weight: 500; margin: 0 0 6px; }
.sub { color: #5F5E5A; font-size: 13px; margin-bottom: 22px; }
.stats { display: flex; flex-wrap: wrap; gap: 10px; margin-bottom: 28px; }
.stat { background: #fff; border: 1px solid rgba(0,0,0,.08); border-radius: 10px;
  padding: 10px 16px; min-width: 118px; }
.stat b { display: block; font-size: 19px; font-weight: 500; }
.stat span { font-size: 12px; color: #5F5E5A; }
h2 { font-size: 16px; font-weight: 500; margin: 34px 0 4px; }
h2 i { font-style: normal; color: #888780; font-size: 13px; margin-right: 6px; }
p.note { font-size: 13px; color: #5F5E5A; margin: 0 0 14px; }
.tokens { display: flex; flex-wrap: wrap; gap: 6px; }
.tk { position: relative; border-radius: 6px; padding: 6px 10px; font-size: 13px;
  border: 1px solid rgba(0,0,0,.10); display: flex; flex-direction: column; min-width: 42px; }
.tk b { font-weight: 500; font-size: 13px; }
.tk em { font-style: normal; font-size: 11px; color: #5F5E5A; }
.tk.lit { background: #E6F1FB; }
.tk.match { background: #FAECE7; }
.tk .idx { position: absolute; top: -7px; left: 4px; font-size: 10px; color: #888780;
  background: #FAF9F5; padding: 0 3px; }
table { width: 100%; border-collapse: collapse; font-size: 13px; background: #fff;
  border: 1px solid rgba(0,0,0,.08); border-radius: 10px; overflow: hidden; }
th, td { text-align: left; padding: 8px 12px; border-bottom: 1px solid rgba(0,0,0,.06);
  vertical-align: top; }
th { background: #F4F2EC; font-weight: 500; font-size: 12px; color: #444441; }
tr:last-child td { border-bottom: none; }
.mono { font-family: "Cascadia Mono", Consolas, monospace; font-size: 12px; }
.small { font-size: 11px; color: #5F5E5A; }
.legend { margin: 0 0 10px; display: flex; flex-wrap: wrap; gap: 12px; font-size: 12px;
  color: #444441; }
.lg i { display: inline-block; width: 12px; height: 12px; border-radius: 3px;
  border: 1px solid rgba(0,0,0,.12); margin-right: 5px; vertical-align: -1px; }
.bits { background: #fff; border: 1px solid rgba(0,0,0,.08); border-radius: 10px;
  padding: 14px; overflow-x: auto; }
.bit { display: inline-block; width: 17px; height: 22px; line-height: 22px; text-align: center;
  font-family: "Cascadia Mono", Consolas, monospace; font-size: 12px; border-radius: 3px;
  margin: 1px; }
.more { font-size: 12px; color: #888780; margin-left: 8px; }
.bytes { display: flex; flex-wrap: wrap; gap: 8px; }
.byte { background: #fff; border: 1px solid rgba(0,0,0,.08); border-radius: 8px;
  padding: 8px 10px; text-align: center; min-width: 64px; }
.byte b { display: block; font-family: "Cascadia Mono", Consolas, monospace; font-size: 14px; }
.byte span { font-size: 10px; color: #888780; font-family: "Cascadia Mono", Consolas, monospace; }
.checks { list-style: none; padding: 0; margin: 0; font-size: 13px; }
.checks li { padding: 8px 12px; background: #fff; border: 1px solid rgba(0,0,0,.08);
  border-radius: 8px; margin-bottom: 8px; }
.checks .ok::before { content: "通过 · "; color: #0F6E56; font-weight: 500; }
.checks .bad::before { content: "失败 · "; color: #A32D2D; font-weight: 500; }
pre { background: #fff; border: 1px solid rgba(0,0,0,.08); border-radius: 10px;
  padding: 12px 14px; font-size: 12px; overflow-x: auto;
  font-family: "Cascadia Mono", Consolas, monospace; color: #2C2C2A; }
code { font-family: "Cascadia Mono", Consolas, monospace; background: #F1EFE8;
  padding: 1px 5px; border-radius: 4px; font-size: 12px; }
"""


def build_html(data: dict, extra: dict) -> str:
    payload = bytes.fromhex(data["text"])
    packed = bytes.fromhex(data["hex"])
    segments = data["segments"]
    tokens = data["tokens"]

    total_stream_bits = sum(seg["bits"] for seg in segments)
    pad_bits = sum(seg["bits"] for seg in segments if seg["kind"] == "pad")
    data_bits = total_stream_bits - pad_bits
    bits = stream_bits(packed, total_stream_bits)
    grid, legend = render_bit_grid(bits, segments, extra["bit_limit"])

    # 位流重新装字节（自洽检查）
    rebuilt = bytearray()
    for start in range(0, len(bits), 8):
        group = bits[start:start + 8]
        value = 0
        for offset, bit in enumerate(group):
            value |= bit << offset
        rebuilt.append(value)
    self_ok = bytes(rebuilt) == packed

    zlib_ok, zlib_msg = extra["zlib"]
    cli = extra["cli"]

    ratio = (1 - data["compressed_size"] / data["input_size"]) * 100 if data["input_size"] else 0
    preview = payload[:80].decode("utf-8", "replace")
    if len(payload) > 80:
        preview += "…"

    # 字节卡片按「写盘顺序」显示位，与上方位流一致（先写入的位在左，但它是字节的低位）
    byte_cards = ""
    for index, byte in enumerate(packed):
        group = bits[index * 8:index * 8 + 8]
        while len(group) < 8:
            group.append(0)
        stream_text = "".join(str(bit) for bit in group)
        byte_cards += f'<div class="byte"><b>{byte:02x}</b><span>{stream_text}</span></div>'

    check_items = [
        f'<li class="{"ok" if self_ok else "bad"}">位流按「先写入的位是低位」重新装回字节，'
        f'结果{"与压缩输出一致" if self_ok else "与压缩输出不一致"}（{data["hex"]}）</li>',
        f'<li class="{"ok" if zlib_ok else "bad"}">{zlib_msg}</li>',
    ]
    if cli and "error" not in cli:
        check_items.append(
            f'<li class="{"ok" if cli.get("restore_ok") else "bad"}">dataguard-cli 备份后还原，'
            f'文件内容{"一致" if cli.get("restore_ok") else "不一致"}</li>'
        )
        check_items.append(
            f'<li class="ok">档案体积：未压缩 {cli["plain"]} 字节 → 压缩后 {cli["compressed"]} 字节'
            f'（省 {cli["plain"] - cli["compressed"]} 字节，{100 * (1 - cli["compressed"] / cli["plain"]):.1f}%）</li>'
        )
    checks = "".join(check_items)

    return f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<title>压缩过程可视化 · DataGuard</title>
<style>{CSS}</style>
</head>
<body>
<div class="wrap">
<h1>这一步到底省在哪儿：压缩过程可视化</h1>
<div class="sub">原文：<code>{preview}</code></div>

<div class="stats">
  <div class="stat"><b>{data["input_size"]}</b><span>原始字节</span></div>
  <div class="stat"><b>{data["compressed_size"]}</b><span>压缩后字节</span></div>
  <div class="stat"><b>{ratio:.1f}%</b><span>压缩率</span></div>
  <div class="stat"><b>{data_bits}</b><span>有效位流位数</span></div>
  <div class="stat"><b>{len(tokens)}</b><span>LZ77 token 数</span></div>
</div>

<h2><i>第一步</i>LZ77 把重复内容换成「回退 + 抄多少」</h2>
<p class="note">蓝色是字面（原样存），橙色是匹配（只存回退距离和长度）。原文 {data["input_size"]} 字节被切成 {len(tokens)} 个 token。</p>
<div class="tokens">{render_tokens(tokens)}</div>

<h2><i>明细</i>每个 token 占多少位</h2>
<table>
<thead><tr><th>#</th><th>token</th><th>对应原文</th><th>位数</th><th>构成</th></tr></thead>
<tbody>{render_table(tokens, segments, payload)}</tbody>
</table>

<h2><i>第二步</i>Huffman 把这些符号写成不等长的位</h2>
<p class="note">下面每一位都是真实输出，按写盘顺序排列；每 8 位之间有一道深色竖线。鼠标悬停可看该位的含义。</p>
<div class="legend">{legend}</div>
<div class="bits">{grid}</div>

<h2><i>落盘</i>{data_bits} 位不够整字节，补 {pad_bits} 个 0 后切成 {data["compressed_size"]} 个字节</h2>
<p class="note">注意：<b>先写入的位放在字节的低位</b>，所以位流看起来「乱」，不能按肉眼顺序读成二进制数。每张卡片下方的 8 位与上方位流一一对应（最左边是这一字节的最低位）。</p>
<div class="bytes">{byte_cards}</div>

<h2><i>验证</i>三项交叉校验</h2>
<ul class="checks">{checks}</ul>

<h2><i>复现</i>换一段文字自己看</h2>
<pre>cd "C:\\Users\\曾俊凯\\Desktop\\软件综合实验"

:: 1) 用内置示例重新生成这一页
make demo

:: 2) 换一段你自己的文本（ASCII 直接写，中文请用 --file）
python tools\\deflate_visualize.py --text "aaaaaaaaaaaaaaaaaaaaaaaa"
python tools\\deflate_visualize.py --file README.md

:: 3) 只看探针的原始输出（JSON）
build\\deflate_probe.exe --text "abcabcabcabcabc"</pre>
<p class="note">页面生成在 <code>build/deflate_demo.html</code>，双击即可打开；每次运行会覆盖。</p>
</div>
</body>
</html>
"""


def main() -> int:
    parser = argparse.ArgumentParser(description="生成 DEFLATE 压缩过程可视化页面")
    parser.add_argument("--text", help="直接指定文本（ASCII）")
    parser.add_argument("--file", help="从文件读取（支持中文与二进制）")
    parser.add_argument("--out", default=str(ROOT / "build" / "deflate_demo.html"))
    parser.add_argument("--bit-limit", type=int, default=512, help="位流最多显示多少位")
    args = parser.parse_args()

    data = run_probe(args.text, args.file)
    payload = bytes.fromhex(data["text"])
    packed = bytes.fromhex(data["hex"])

    html = build_html(data, {
        "bit_limit": args.bit_limit,
        "zlib": zlib_check(packed, payload),
        "cli": cli_roundtrip(payload),
    })

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(html, encoding="utf-8")

    print(f"原文 {data['input_size']} 字节 → 压缩 {data['compressed_size']} 字节，"
          f"token {len(data['tokens'])} 个，位流 {sum(s['bits'] for s in data['segments'])} 位")
    print(f"已生成：{out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

# DataGuard 系统设计文档

> 版本：v0.1（骨架与基础模块）｜最后更新：2026-10-18
> 本文档按版本递增补充，v1.0 定稿。UML 图使用 Mermaid，可在 GitHub / VS Code / mermaid.live 直接渲染；如需 StarUML 工程文件，可按同名结构复刻。

## 1. 系统架构

```mermaid
graph TB
    subgraph 表现层
        GUI[PySide6 GUI]
        CLI[dataguard-cli.exe]
    end
    subgraph 应用层
        BM[BackupManager / RestoreManager]
        SCHED[定时调度 scheduler.py]
    end
    subgraph 核心服务层
        SCAN[FileScanner]
        FILT[Filter]
        AW[ArchiveWriter / ArchiveReader]
    end
    subgraph 算法层
        DEF[DeflateCompressor]
        CIPHER[StreamCipher]
        CRC[CRC32]
    end
    subgraph 系统层
        FS[std::filesystem]
        WIN[Win32 API]
    end
    GUI -->|subprocess + 行协议| CLI
    CLI --> BM
    BM --> SCAN --> FILT
    BM --> AW
    AW --> DEF
    AW --> CIPHER
    AW --> CRC
    SCAN --> FS
    SCAN --> WIN
```

## 2. 实现决策记录（v0.1 确认）

| 功能 | 决策 | 理由 | 分数影响 |
|---|---|---|---|
| 打包解包 | 自研 BKPK v1 容器格式 | 备份功能本身需要自定义存储格式，自研无额外成本 | 满分 10 |
| 压缩解压 | 自研 DEFLATE（LZ77 + 固定 Huffman，RFC 1951 子集） | 用户明确要求 DEFLATE；LZ77+Huffman 是课程技术亮点 | 满分 10 |
| 加密解密 | 自研流密码（KSA/PRGA + 随机 IV） | 禁止简单异或；实现正确性可由自检用例保证 | 满分 10 |
| 图形界面 | Python 3.11 + PySide6 | 本机无 Qt C++ 环境；README 允许脚本语言写界面 | 满分 10 |
| 定时调度 | Python 端实现（守护线程 + tasks.json） | 与 GUI 同进程，避免 C++ 依赖 Qt 定时器 | 满分 10 |

## 3. DEFLATE 类族设计（v0.3 实现）

```mermaid
classDiagram
    class Compressor {
        <<interface>>
        +id() uint8_t
        +compress(in, out) bool
        +decompress(in, out) bool
        +name() const char*
    }
    class DeflateCompressor {
        +id() 1
        +compress(in, out) bool
        +decompress(in, out) bool
    }
    class BitWriter {
        +writeBits(value, nbits)
        +writeHuffman(code, nbits)
        +alignToByte()
        +finish() vector~uint8_t~
    }
    class BitReader {
        +readBits(nbits, value) bool
        +readHuffman(nbits, code) bool
        +eof() bool
    }
    class FixedHuffmanTable {
        +literalLength(symbol) HuffmanEntry
        +distance(code) HuffmanEntry
        +decodeLiteralLength(reader, symbol) bool
        +encodeLength(length, symbol, extra_bits, extra_value) bool
        +encodeDistance(distance, symbol, extra_bits, extra_value) bool
    }
    class Lz77Matcher {
        +tokenize(input) vector~Lz77Token~
    }
    Compressor <|-- DeflateCompressor
    DeflateCompressor --> Lz77Matcher
    DeflateCompressor --> FixedHuffmanTable
    DeflateCompressor --> BitWriter
    DeflateCompressor --> BitReader
```

职责划分：
- `Lz77Matcher`：32KB 滑窗 + 3 字节哈希链，输出 literal / (length, distance) token 序列；
- `FixedHuffmanTable`：RFC 1951 固定码表，负责码字查询、长度/距离码与值的映射、符号解码；
- `BitWriter` / `BitReader`：位级读写，普通数据 LSB-first、Huffman 码 MSB-first；
- `DeflateCompressor`：编排上述三类，完成压缩/解压。

## 4. 版本计划

| 版本 | 内容 |
|---|---|
| v0.1（10-18） | 工程骨架、CRC32、路径工具、筛选器、扫描器、Windows 元数据、单元测试 |
| v0.2（10-19） | BKPK 档案格式、BackupManager/RestoreManager、CLI 行协议 |
| v0.3（10-20） | DEFLATE 压缩实现并接入档案流水线、11 个算法自检用例 |
| v0.4（10-21） | 流密码加密、六类筛选完善、特殊文件与元数据还原、retention 淘汰 |
| v0.5（10-21） | PySide6 界面、定时调度与任务持久化 |
| v1.0（10-21） | 需求/设计/测试报告定稿、全量回归、发布 |

## 5. v0.2 模块说明（档案格式与备份还原）

### 5.1 BKPK v1 档案格式

```
┌────────────┬──────────────────────────────┬──────────────┐
│ FixedHeader│ Entry Table（可整体加密）     │ Data Region  │
│ 38 字节    │ N × EntryRecord（变长）      │ （可整体加密）│
└────────────┴──────────────────────────────┴──────────────┘
```

FixedHeader（小端，iv_len=0 时共 38 字节）：

| 偏移 | 大小 | 字段 | 说明 |
|---|---|---|---|
| 0 | 4 | magic | `0x4B504B42`（'BKPB'） |
| 4 | 2 | version | 1 |
| 6 | 2 | flags | bit0=压缩, bit1=加密 |
| 8 | 1 | compressor_id | 0=none（v0.2）, 1=deflate（v0.3） |
| 9 | 1 | cipher_id | 0=none（v0.2）, 1=stream（v0.4） |
| 10 | 4 | entry_count | 条目数 |
| 14 | 4 | table_offset | 34 + iv_len + 4 |
| 18 | 4 | data_offset | table_offset + 表字节数 |
| 22 | 4 | header_crc | CRC32(字节[0..22)) |
| 26 | 4 | iv_len | 加密时 16，否则 0 |
| 30+iv | 4 | password_check | 加密口令校验值 |
| 34+iv | 4 | table_crc | CRC32(条目表) |

EntryRecord 字段顺序：name_len(u32) → kind(u8) → size(u64) → stored_size(u64) →
mtime/ctime/atime(i64×3) → attrs(u32) → owner_len(u32) → link_target_len(u32) →
file_id(u64) → checksum(u32) → offset(u64) → name/owner/link_target（UTF-8 变长）。

### 5.2 类关系

```mermaid
classDiagram
    class BackupManager {
        +run(cfg, reporter) bool
        +last_archive() path
        +last_success_count() uint64
    }
    class RestoreManager {
        +run(archive, dest, password, overwrite, only_files, reporter) bool
    }
    class ArchiveWriter {
        +open(path, options) bool
        +add(entry) bool
        +close() bool
    }
    class ArchiveReader {
        +open(path, password) bool
        +entries() vector~FileEntry~
        +extract(entry, dest) bool
        +extractAll(dest) bool
        +verify(bad_files) bool
    }
    class FileScanner
    class Filter
    class FileEntry
    BackupManager --> FileScanner
    BackupManager --> ArchiveWriter
    RestoreManager --> ArchiveReader
    ArchiveWriter --> FileEntry
    ArchiveReader --> FileEntry
    FileScanner --> Filter
```

### 5.3 备份/还原流程

```mermaid
sequenceDiagram
    participant CLI as dataguard-cli
    participant BM as BackupManager
    participant SC as FileScanner
    participant AW as ArchiveWriter
    participant FS as 文件系统
    CLI->>BM: run(cfg)
    BM->>SC: scan(source, filter)
    SC-->>BM: entries[]
    BM->>AW: open(archive, options)
    loop 每个条目
        BM->>AW: add(entry)
        AW->>FS: 读取文件内容 + 计算 CRC32
    end
    BM->>AW: close()
    AW->>FS: 写 FixedHeader + Entry Table + Data
    BM-->>CLI: DONE 成功数/失败数/档案路径
```

### 5.4 v0.2 模块清单

| 模块 | 文件 | 职责 |
|---|---|---|
| ArchiveWriter | src/archive/archive_writer.cpp | 序列化条目（路径/类型/大小/三时间/属性/属主/链接/CRC/偏移），close 时写出 header+表+数据 |
| ArchiveReader | src/archive/archive_reader.cpp | 校验 magic/版本/header_crc/table_crc，解析条目表，按偏移取数据并校验 CRC，还原文件/目录/符号链接 |
| BackupManager | src/app/backup_manager.cpp | 扫描 → 逐条写档案 → 输出进度；失败条目计数不中断 |
| RestoreManager | src/app/backup_manager.cpp | 打开档案 → 按需选择条目 → 还原；支持 `--no-overwrite` 与 `--files` 子集 |
| CLI | src/cli/main.cpp | backup/restore/list/verify 命令；宽字符命令行解析保证中文路径；stdout 行协议 |


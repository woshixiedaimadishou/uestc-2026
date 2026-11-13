# DataGuard 1.0.0 发布说明

- **版本**：1.0.0
- **发布日期**：2026-11-13
- **平台**：Windows 10 / 11（x64）
- **仓库**：https://github.com/woshixiedaimadishou/uestc-2026

## 下载哪个

| 资产 | 大小 | 用途 |
|---|---|---|
| `DataGuard-1.0.0-win64.zip` | 35.7 MB | **推荐**。便携版，解压双击 `DataGuard.exe`，不需要 Python 或编译器 |
| `DataGuard-1.0.0-git-archive.zip` | 0.1 MB | 与 tag `v1.0` 完全一致的源码快照 |
| `DataGuard-1.0.0-提交包-含可执行程序.zip` | 35.8 MB | 课程提交用：源码 + 可执行程序 |
| `DataGuard-1.0.0-提交包-仅源码.zip` | 0.1 MB | 平台有大小限制时用（74 个文件） |

SHA256：

```
2c88f7b6afd6de2871057f2bea54645ea5dd9d3557e64396a44597f2210e00f9  DataGuard-1.0.0-win64.zip
60af9d2deed2b490b3e630b8b762c8086e917bfbbb68e4a0af6aa1fde3e0ca7b  DataGuard-1.0.0-git-archive.zip
2ae5aa95fac211dd034bbc54ea943266a52a44201bc591d80c472e6cd0bd6507  DataGuard-1.0.0-提交包-含可执行程序.zip
85c3d5f95b1073a9ad2533d17bae7b8595e6e818268822cb6177b7c95351f550  DataGuard-1.0.0-提交包-仅源码.zip
```

## 使用

1. 解压**整个** `DataGuard` 文件夹（`_internal` 是运行时依赖，不能只复制 exe）
2. 双击 `DataGuard.exe`
3. 新建备份 → 选源目录 / 目标目录 / 任务名 → 选压缩方案 → 开始备份
4. 还原时点「还原备份」→ 选档案 → 勾选条目（勾目录会连子文件一起还原）

命令行：

```
dataguard-cli.exe --version
dataguard-cli.exe backup <源目录> <目标目录> --name 任务名 --codec deflate
dataguard-cli.exe list|verify|restore <档案> [目标目录]
```

## 功能

- **备份 / 还原**：目录树打包成 `.bkpk`，保留修改时间、属性、硬链接、符号链接、空目录
- **三种压缩方案**：DEFLATE（LZ77+Huffman）/ 仅 Huffman / 仅 LZ77 / 不压缩
- **加密**：口令 + 16 字节随机 IV 的流密码，错误口令快速拒绝
- **六类筛选**：扩展名、路径 glob、文件名、修改时间、尺寸、属主
- **定时任务**：立即 / 每天 / 每周 / 每月 / 一次性，任务 JSON 持久化
- **数据淘汰**：按份数或天数保留最近档案
- **局部还原**：条目树勾选，全选等价整档还原
- **完整性校验**：`verify` 逐条重算 CRC

## 自研算法（不使用 zlib 等第三方压缩库）

| 模块 | 实现 |
|---|---|
| 压缩 | LZ77（32KB 窗口、3 字节哈希链）+ 固定 Huffman（RFC 1951 子集）；另有动态 Huffman 与纯 LZ77 两套方案 |
| 加密 | 类 RC4 流密码：key = 口令 ‖ IV，KSA 256 轮 + 丢弃前 256 字节 + PRGA 逐字节异或 |
| 容器 | BKPK 格式：38 字节固定头（magic/版本/算法标识/偏移/双 CRC）+ 条目表 + 数据区 |

## 质量

- C++ 单元测试 **85** 个（gtest）：CRC32、筛选、扫描、档案、DEFLATE、三套编码方案、加密、端到端备份还原、数据淘汰
- Python 单元测试 **70** 个（unittest）：任务持久化、调度边界、行协议、参数组装、选择模型、数字归一化
- 集成测试 **6** 组场景：plain / deflate / huffman / lz77 / 加密 / 压缩+加密，外加局部还原与 1200 条目大批量
- 性能（23914 文件 / 443 MB）：不压缩 8.2 s，DEFLATE 10.6 s

## 从源码构建

```bash
# 依赖：w64devkit（g++ 12+）、Python 3.11、PySide6 6.8、gtest 源码
make -j          # 生成 build/dataguard-cli.exe
make test        # C++ 单元测试
make test-py     # Python 单元测试
make integration # 集成测试
make dist        # 打包 dist/DataGuard/DataGuard.exe
make demo        # 生成压缩过程可视化页面 build/deflate_demo.html
```

Linux 下构建：核心库为标准 C++17（`src/core`、`src/archive`、`src/algo`、`src/app`），
但 `src/win/win_metadata.cpp` 依赖 Win32 API，需要替换为 POSIX 版本；界面层依赖 PySide6，
可在 Linux 直接运行源码（`python python/dataguard_gui.py`），但需要先把元数据模块做成跨平台实现。

## 已知限制

- 仅支持 Windows（元数据与属主读取使用 Win32 API）
- 单文件上限 1 GB；超限文件跳过并告警
- 备份时待打包内容读入内存，数十 GB 级目录建议分批
- 加密算法为课程演示实现，不建议用于真实敏感数据（无完整性认证、口令未做 KDF）

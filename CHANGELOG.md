# 变更记录（CHANGELOG）

DataGuard 数据备份软件 · 课程项目
仓库：https://github.com/woshixiedaimadishou/uestc-2026

版本号遵循「每完成一个模块并测试通过后提交」的节奏，从 0.1 递增到 1.0。

---

## v1.0（2026-11-13）— 最终发布

**收尾与交付**

- 版本号统一为 1.0.0（CLI `--version`、GUI 标题、发布包命名一致）
- 新增 GitHub Release 发布包（便携版 zip，解压即用，无需 Python/编译器）
- 新增 `tools/package_release.py`：一键生成「发布包」与「课程提交包」
- 新增 `CHANGELOG.md` 与 `dist/DataGuard/使用说明.txt`
- 全量回归：C++ 85 个单元测试 + Python 70 个单元测试 + 集成 6 组场景

## v0.6（2026-11-03）— 三种压缩方案与性能优化

**新增压缩方案**（档案头 `compressor_id` 区分，读取自动识别）

| id | 方案 | 说明 |
|---|---|---|
| 0 | none | 不压缩 |
| 1 | deflate | LZ77 + 固定 Huffman（RFC 1951 子集，默认推荐） |
| 2 | huffman | 仅动态 Huffman，码长表 RLE 存储 |
| 3 | lz77 | 仅 LZ77，token 位打包 |

- CLI：`--codec deflate|huffman|lz77|none`（`--compress` 保留为 deflate 别名）
- GUI：向导第 3 步改为「压缩方案」下拉框；任务配置 `options.codec`，旧 `compress` 自动迁移

**性能优化**（实测 23914 个文件 / 443 MB）

| 阶段 | 优化前 | 优化后 |
|---|---|---|
| 扫描 + 元数据 | 11.2 s | 5.8 s |
| 打包（不压缩） | 20.5 s | 8.2 s |
| 打包（DEFLATE） | 29.9 s | 10.6 s |

- 属主 SID→用户名缓存（省去 2.4 万次 LSA 查询）
- 一次 `CreateFileW` 取全属性（原来 3 次系统调用）
- `close()` 改为流式写出（去掉 443 MB 整包拷贝）
- `StreamCipherStream` 分块加密，密钥流跨块连续（与旧档案兼容）
- 读文件与压缩并行（上限 8 线程），登记顺序不变，档案字节确定

**界面**

- 设置中的默认备份目录持久化到 `dataguard_settings.json`
- 还原选择档案、备份源/目标目录的对话框从默认备份目录开始

## v0.5.2（2026-10-21）— 压缩过程可视化

- `tools/deflate_probe.cpp`：输出 LZ77 token 流、位流分段、压缩字节的 JSON
- `tools/deflate_visualize.py`：生成单文件 HTML（token 着色、位数明细、位流格子、字节卡片、三项交叉校验）
- 交叉校验包含 `zlib.decompress(..., -15)`，证明生成的位流符合 RFC 1951
- `make demo` 一键生成页面

## v0.5.1（2026-10-20）— 缺陷修复

- **数字显示**：系统区域为 zh_HK 时 Qt 用中文数字渲染日期/时间（`08:00` → `〇〨:〇〇`）；改为实测 `QLocale().toString(8)` 后切换区域，并新增 `text_utils.to_ascii_digits()` 归一化
- **还原报 `WinError 206`**：勾选条目拼成单个 `--files` 参数导致命令行超 32767 字符；改为写 UTF-8 清单文件 + `--files-from`
- **还原选择逻辑重写**：`selection_model.py`（父子三态、清单压缩），全选等价整档还原
- C++ 子集还原支持目录前缀匹配；选择器全部不存在时返回失败

## v0.5（2026-10-20）— 图形界面与发布打包

- PySide6 图形界面：备份历史、任务管理、设置三个页面
- 4 页向导：源目标 → 六类筛选 → 压缩/加密 → 计划与保留
- 定时调度（manual/once/daily/weekly/monthly）+ 任务 JSON 持久化
- 还原对话框：条目树勾选、覆盖选项、后台进度
- PyInstaller 打包为可双击运行的 `DataGuard.exe`

## v0.4（2026-10-19）— 加密、筛选完善与数据淘汰

- 自研流密码（口令 + 16 字节随机 IV，KSA/PRGA，加密与解密同一操作）
- 口令校验值（`DG-PASS-CHECK` 加密前 4 字节），错误口令快速拒绝
- 六类筛选完善：扩展名 / 路径 glob / 文件名 / 修改时间 / 尺寸 / 属主
- 特殊文件（符号链接、硬链接、空目录）与元数据（时间、属性）还原
- 数据淘汰（retention）：按份数或天数保留

## v0.3（2026-10-18）— DEFLATE 压缩

- LZ77 滑动窗口匹配（32KB 窗口、3 字节哈希链、最长 258）
- 固定 Huffman 编码（RFC 1951，MSB-first）与长度/距离码表
- 位流读写（LSB/MSB 两套位序）、单块输出、块结束符 256
- 接入档案流水线：压缩在 CRC 计算之后，校验始终基于原始数据

## v0.2（2026-10-18）— 档案格式与备份还原

- BKPK 容器格式：38 字节固定头 + 条目表 + 数据区，含 header_crc / table_crc
- 备份主流程：扫描 → 读取 → 校验 → 写入 → 落盘
- 还原主流程：头部校验 → 条目表解析 → 逐条解出 → 元数据恢复
- CLI 行协议：`PROGRESS` / `LOG` / `DONE`，供图形界面与集成脚本解析

## v0.1（2026-10-18）— 工程骨架

- Makefile（w64devkit g++，`-static` 链接）、目录结构、头文件接口契约
- CRC32、路径工具（UTF-8 ↔ 宽字符）、筛选器、文件扫描器
- Windows 元数据读取（CreateFileW + GetFileInformationByHandle + 属主 SID）
- gtest 单元测试框架接入

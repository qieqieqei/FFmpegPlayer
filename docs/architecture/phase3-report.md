# 阶段 3 报告：目标目录骨架 + legacy 隔离

> 提交：`d1732b4`（源码/工程文件） + 本文档（docs）
> 前置基线：`2af4d01`（tag `architecture-audit-baseline-2af4d01`）→ `ad4aaed`（阶段1 docs）→ `c8ec44e`（阶段2 docs）

## 1. 本阶段做了什么

**纯移动，零逻辑改动。** 把 118 个源文件按《target-architecture.md》的目录布局用 `git mv` 重排，并把 18 个 legacy 文件隔离到 `legacy/`（**只搬不删**，处置推迟到阶段 6）。

### 目录分布（移动后）

| 目录 | 文件数 | 目录 | 文件数 |
|---|---|---|---|
| app/ | 3 | output/audio/ | 2 |
| config/ | 4 | output/osd/ | 4 |
| core/ | 3 | output/video/ | 2 |
| features/playlist/ | 2 | pipeline/audio/ | 12 |
| features/screenshot/ | 2 | pipeline/demux/ | 2 |
| features/seek/ | 2 | pipeline/input/ | 8 |
| features/statistics/ | 2 | pipeline/queue/ | 4 |
| features/subtitle/ | 2 | pipeline/video/ | 2 |
| hardware/ | 4 | recording/ | 12 |
| infra/ | 6 | streaming/ | 8 |
| sync/ | 14 | **legacy/** | **18** |
| | | **合计** | **118** |

### legacy/ 隔离清单（18 个）

- 未参与编译（不在 vcxproj）：`Decoder.{h,cpp}`、`Input.{h,cpp}`、`Screenshot.{h,cpp}`、`AudioMixer.{h,cpp}`
- 已登记 vcxproj 但外部无引用：`Clock.{h,cpp}`、`FilterGraph.{h,cpp}`、`AudioFilter.{h,cpp}`、`VideoFilter.{h,cpp}`、`RTSPClient.{h,cpp}`

> 判定依据为**编译列表 + 外部引用视角**，不是"目录里没人 include"。删除一律推迟到阶段 6，需 源码引用 + 构建依赖 + 运行路径 三重确认。

## 2. 关键风险与处理

### 2.1 项目使用根相对 include
项目 include 约定以 `$(ProjectDir)` 为根（如 `#include "Sync/Clock.h"`），子目录下移**不会**自动生效 → 必须同步改写 include 路径串。

- 改写 include：**222 处 / 89 个文件**
- 更新工程文件：`FFmpeg_text_claw.vcxproj` 109 处、`.vcxproj.filters` 99 处

### 2.2 源码是 GBK 编码（重要教训）
MSVC 未加 `/utf-8`，源码实际为 **GBK**。第一版脚本按 UTF-8 读写 → 中文注释/字符串被破坏（diff 出现 `锟斤拷`），**编译仍 0 error，但字符串已损坏 = 隐蔽行为回归**。

- 修复：全部改用以 **latin1（裸字节，1 byte ↔ 1 char 无损）** 读写，只对 ASCII 的 `#include "..."` 做替换；
- 脚本内置断言：抹掉 include 字符串后，改动前后内容必须逐字节相等。

> **规则：改本项目任何源文件前先确认编码，用 latin1/二进制处理，禁止 utf8。**

### 2.3 CRLF 差异是 diff 伪影
工作树为 CRLF、blob 为 LF（`.gitattributes` `* text=auto` + `core.autocrlf=true`）。diff 里偶发的 `-空行/+空行` 经核实为 **diff 对齐伪影**：抹掉 include 后新旧 blob 完全一致。

## 3. 验证证据

- **改动内容审计（118 个文件对全量）**：抹掉 include 字符串后内容逐字节相同 → **0 失败**；89 个文件仅有 include 变化，共 222 处。
- **工程文件审计**：vcxproj / filters 的 `+/-` 行 100% 为 `Include=` 路径。
- **编译**：
  - `MSBuild Debug|x64` → 退出码 0，error 0
  - `MSBuild Release|x64` → 退出码 0，error 0
- **工作树**：提交后干净（未跟踪/未提交条目 = 0）。

## 4. 未做的事（明确边界）

- 未删除任何文件；未修改任何函数逻辑；未抽新类；未动 Player 内部结构。
- Player 仍是"上帝对象"（`core/Player.cpp` 4651 行）——拆分属阶段 4。

## 5. 下一步（阶段 4）

抽 `PlaybackSession` + `MediaContext`，Player 自身收敛到只做"对外 API / 会话生命周期 / 高层状态 / 模块协调"（目标 200~500 行，禁止机械拆文件）。

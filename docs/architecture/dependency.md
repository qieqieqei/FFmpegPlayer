# 依赖方向设计（阶段 2）

> **阶段 2 交付物 · 设计文档（未修改任何源码）**
> 基线：`2af4d01`。本文配套 `target-architecture.md`（目标目录/职责）与 `migration-map.md`（迁移表）。

---

## 1. 现状依赖（实测）

### 1.1 反向 include 图（"谁包含了它"）——关键项

| 被包含的头 | 包含它的文件（项目内） | 判断 |
|---|---|---|
| `Player.h` | `Player.cpp`, `main.cpp`, **`Renderer.cpp`**, **`OSDManager.cpp`**, **`Event.cpp`** | ❌ 3 个下层模块反向依赖核心 |
| `Player.h`（前置声明） | `Renderer.h:13`, `OSDManager.h:7`, `Event.h:5`（均 `class Player;`） | 头文件干净，问题在 .cpp |
| `ErrorHandler.h` / `FFmpegPtr.h` | ~20+ 文件 | ✅ 正常（Infra） |
| `Demuxer.h` | `Demuxer.cpp`, `Player.h`, `SeekController.cpp` | ⚠️ `SeekController`（features）依赖 pipeline |
| `PacketQueue.h` / `FrameQueue.h` | `...`, `Player.h`, `SeekController.cpp`, `Decoder.h` | 同上 |
| `StreamConfig.h` | `ConfigManager.h`, `Demuxer.h`, `Input/*`, `RTMPPublisher.h`, `StreamMonitor.h`, `RTSPClient.h` | ⚠️ 配置结构被多层引用（见 §2 讨论） |

### 1.2 头文件级循环依赖

**实测：无。** 对全部 ~55 个头文件做可达性检测（`*.h → *.h`），未发现任何头可回到自身。
即"循环依赖"在**头文件级不存在**；现存的是**反向耦合（非循环）**，见 §1.1。

### 1.3 反向依赖的具体调用（Renderer / OSD / Event → Player）

| 文件 | 引用数 | 代表 |
|---|---:|---|
| `Renderer.cpp`（`:4` include） | ~20 | `GetSwsForFrame`/`GetRGBData`/`GetRGBLinesize`/`GetRGBTexture`/`GetWindow`/`GetState`/`GetProgress`/`GetControlBar`… |
| `OSDManager.cpp`（`:5` include） | ~8 | `GetStatistics`/`StateToString`/`GetTimeString`/`GetNetworkStatistics`/`IsHardwareDecode`… |
| `Event.cpp`（`:3` include） | ~25 | `TogglePause`/`ToggleFullScreen`/`RequestSeek`/`SetPlaybackSpeed`/`SetVolume`/`PlayPrevious`/`PlayNext`/`ToggleRecording`/`GetControlBar`… |

---

## 2. 目标依赖矩阵

行 = 依赖方（上层），列 = 被依赖（下层）。“✓”允许，“✗”禁止，“—”自身/无关系。

| ↓依赖方 \ 被依赖→ | infra | config | hardware | pipeline | sync | output | streaming | recording | features | core | app |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| **infra** | — | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |
| **config** | ✓ | — | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |
| **hardware** | ✓ | ✓ | — | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |
| **pipeline** | ✓ | ✓ | ✓ | — | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |
| **sync** | ✓ | ✗ | ✗ | ✓ | — | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |
| **output** | ✓ | ✓ | ✗ | ✓ | ✓ | — | ✗ | ✗ | ✗ | ✗ | ✗ |
| **streaming** | ✓ | ✓ | ✗ | ✓ | ✗ | ✗ | — | ✗ | ✗ | ✗ | ✗ |
| **recording** | ✓ | ✓ | ✓ | ✓ | ✗ | ✗ | ✗ | — | ✗ | ✗ | ✗ |
| **features** | ✓ | ✓ | ✗ | ✓ | ✓ | ✗ | ✗ | ✗ | — | ✗ | ✗ |
| **core** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — | ✗ |
| **app** | — | — | — | — | — | — | — | — | — | ✓ | — |

要点：

- **`output`/`features`/`sync`/`pipeline`/`infra` 一律不得依赖 `core`**（当前 `output`(Renderer/OSD) 违反）。
- `app` 只依赖 `core`（当前 `Event` 违反：直接依赖 Player 本体；目标改为依赖 `PlayerFacade`）。
- `core` 是唯一可以"知道全部"的层（但通过 `MediaContext` 持有，不做实现）。

**关于 `StreamConfig.h` 被多层引用**：它是**纯数据结构**（无逻辑），归 `config` 后，`pipeline/recording/streaming` 引用它属"✓ 依赖 config"，合规；但应避免把逻辑塞进该结构。

---

## 3. 反向依赖拆解方案（阶段 3~8 执行）

### 3.1 `Renderer`（output/video）

| 现状 | 目标 |
|---|---|
| `Renderer.cpp` include `Player.h`，调用 ~20 个 `player->Get*` | Renderer 改为接收**数据上下文**（`RenderContext`：纹理/RGB 缓冲/尺寸/进度/状态快照），由 `PlaybackSession` 传入 |
| `Renderer::InitSDL(...)` 用 out 参数建 SDL 窗口/渲染器/纹理；`Player::ReleaseMedia` 销毁 | 所有权归一：**创建与销毁同一所有者**（建议归 Renderer，或归 `output/video/SdlContext`），消除跨模块双所有权 |
| `ActiveVideoFrame` 线程本地缓冲（`Renderer.cpp:14`） | 保留在 Renderer 内 |

### 3.2 `OSDManager`（output/osd）

| 现状 | 目标 |
|---|---|
| include `Player.h`，调用 `GetStatistics/StateToString/...` | 改为接收**只读统计快照**（`StatsSnapshot` 结构），不再持有 `Player*` |

### 3.3 `Event`（app/input）

| 现状 | 目标 |
|---|---|
| include `Player.h`，直接 `TogglePause/RequestSeek/...`；访问 `Player::ControlBarState&` | 改为调用 **`PlayerFacade`**；`ControlBarState` 移入 `app/ControlBar.h`，由控制层持有 |

### 3.4 `SeekController`（features/seek）

| 现状 | 目标 |
|---|---|
| `SeekController.cpp` include `Demuxer.h`/`PacketQueue.h`/`FrameQueue.h` | 接受一个 **`SeekTarget` 接口**（`OnSeek(pts)` + 队列 Flush/Interrupt 能力），不直接包含具体 pipeline 类型（若保留直接依赖，也属"features→pipeline ✓"，可接受——**按阶段 6 再定**，勿提前抽象） |

---

## 4. 验收口径（依赖部分）

1. `grep -r '#include "Player.h"' --include=*.cpp`：**只允许 `core/` 内与 `app/`**（且 app 需经 Facade 语义）出现。
2. 头文件级循环：保持为 0（每次迁移后重跑检测脚本）。
3. 依赖矩阵中"✗"单元格：用脚本抽查（如 `output/*` 不得出现 `core/` 头）。
4. 每步迁移后 `MSBuild Debug/x64` 退出码 0；阶段末 `Release/x64` 退出码 0。

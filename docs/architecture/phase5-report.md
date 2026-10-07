# 阶段 5 报告：管线 / 呈现职责外移与 Run() 主循环收敛

> 目标：继续把**管线 / 呈现 / 主循环编排**职责从 `Player` 移出，让 `Run()` 收敛为“编排骨架”。
> 起点：`core/Player.cpp` 3060 行（`Run()` 645 行）、`PlaybackSession.cpp` 1626 行。
> 硬约束沿用 `refactor-rules.md`：先审计后改；禁止一次性大重构；禁止机械拆文件；不改行为；不顺手修 Bug；不确定不删。

## 1. 提交链（分支 `feature/live-buffer`，均已推送 origin）

| 提交 | 内容 |
|---|---|
| `4bd1306` | 阶段 5 计划文档 `docs/architecture/phase5-plan.md` |
| `3c7cf79` | **5.1** 队列路由外移 → `PlaybackSession` |
| `29e7006` | docs：回填 5.1 提交哈希 |
| `43fd50f` | **5.2** 呈现簇外移 → 新 `output/video/VideoPresenter` |
| `e2a7d79` | docs：回填 5.2 提交哈希 |
| `3cdbc72` | **5.3** `Run()` 渲染/呈现段外移 → `PlaybackSession::PresentFrame` |
| `08dbddf` | docs：记录 5.3 执行 |
| `6196853` | **5.4** `Run()` switch / reconnect 编排外移 |
| `d29766d` | docs：记录 5.4 执行 |
| `56dd867` | **5.3b** `Run()` 取帧 + A/V 同步 + 丢帧外移 |
| `b2c4c14` | docs：记录 5.3b 执行 |

## 2. 结果指标

| 文件 | 阶段 5 前 | 阶段 5 后 | 非空行（后） |
|---|---|---|---|
| `core/Player.h` | 533 | **476** | 315 |
| `core/Player.cpp` | 3060 | **2348** | 1828 |
| `core/MediaContext.h` | 70 | **96** | 70 |
| `core/PlaybackSession.h` | 113 | **175** | 114 |
| `core/PlaybackSession.cpp` | 1626 | **2301** | 1826 |
| `output/video/VideoPresenter.h` | — | **107** | 76 |
| `output/video/VideoPresenter.cpp` | — | **206** | 161 |

| 关键热点 | 阶段 5 前 | 阶段 5 后 |
|---|---|---|
| `Player::Run()` | 645 | **110** |

> 计划预期：阶段结束后 `Player.cpp` ≈2400~2600 行、`Run()` ≈300~350 行。
> 实际 **`Player.cpp` 2348 行**、**`Run()` 110 行**，两项均优于预期 —— 因为 5.3/5.3b 把 `Run()` 的呈现段与取帧/同步/丢帧段**整段**外移，
> `Run()` 现在只剩：启动三线程 → 事件处理 → switch/reconnect 分派 → 字幕更新 → `AcquireAndSyncFrame` + `PresentFrame` → 退出收尾。
> 剩余大头（输出链 ≈800 行、统计 198、`Init` 117）按计划属**阶段 6 / 7**。

## 3. 职责归属

- **`output/video/VideoPresenter`（新增，按值持有于 `MediaContext`）**：SDL 资源（`window/renderer/texture/rgbTexture`）
  + `swsCtx/swsSrc*` + `rgbData/rgbLinesize` + `lastFrame`；方法 `Create/Destroy/ApplyFullscreen/GetSwsForFrame/SetLastFrame/Get*`。
  生命周期 = 每个媒体（`OpenMedia` 建、`ReleaseMedia` 销），与既有 SDL 资源的 born/death 点精确吻合。
- **`PlaybackSession` 新增**：
  - 队列路由（5.1）：9 个访问器 + `lastVideoDropped/lastAudioDropped`；
  - `PresentFrame(AVFrame*, double pts, bool& quit, bool& lastBufferingBlock)`（5.3）；
  - `HandleSwitchRequest(bool& quit)` / `HandleReconnect(bool& quit)`（5.4）；
  - `AcquireAndSyncFrame(FramePtr&, double& pts, bool& quit, bool& lastBufferingBlock)` + 嵌套枚举 `FrameAction{ Skip, Present }`（5.3b）。
- **`Player`（保留）**：Facade/UI 访问器、输出链（编码/录制/推流/HLS）、统计 `UpdateStatistics`、`Init`、`Close`、`Run/GetFramePts/HasAudio`。

### 依赖方向
- `VideoPresenter` 只接**数据**（不持 `Player*`），`output` 不依赖 `core`；`Renderer` 仍持 `Player*` 的**既有反向依赖**留阶段 8 拆。
- `HandleEvent(quit, &owner)` 是 **app 层自由函数**（`app/Event.h`）→ `PlaybackSession.cpp` 新增 `#include "app/Event.h"`，
  与 `Player.cpp` 既有 `core → app` 同类引用；登记为**阶段 8 收口项**。
- 会话内 `media` 为引用 → `media.`；Player 成员访问经 `owner.`（`friend class PlaybackSession;` 已建）。

## 4. 验证证据（每一子步都重跑）

- **构建**：`MSBuild FFmpeg_text_claw.vcxproj /p:Configuration=Debug /p:Platform=x64` 与 `Release/x64` 均
  **exit 0 / error 0**；两侧 warning 均为 **32 行**（`C4828` 全部来自既有 `output/osd/FontManager.h`、`C4244` 为既有 `Player.cpp`，**无新增告警类型**）。
- **功能回归**（`x64\Release` 就地运行，`--record` + `autoQuitOnEof`）：

  | 样例 | 退出码 | 录制 FLV 时长 | 录制 FLV 字节 |
  |---|---|---|---|
  | `124662f108eca04d9189d0efae3829c7.mp4` | 0 | 12.833 s | 7280913 |
  | `21e1626495c5d9174868eebab99e437c.mp4` | 0 | 22.655 s | 9666764 |
  | `a4c277.mp4` | 0 | 141.800 s | 59694920 |

  **三个 FLV 字节数与阶段 4 基线逐位一致**，`dropFrame=0 / lateDrop=0`，日志 ERROR/WARN = 0。
  （`--record` 走完整 demux→解码→渲染→编码→复用链路，覆盖 5.3/5.3b 的取帧/同步/丢帧路径。）

## 5. 设计决策与偏差

1. **5.3 落点从 `VideoPresenter` 改为 `PlaybackSession::PresentFrame`**（已记入 plan §6）：
   该段需要 core 侧状态（`statistics/networkStatistics/bufferController/syncController/SetCurrentTime/UpdateStatistics`），
   放进 `output/` 会新增 `output → core` 反向依赖（`dependency.md` 禁止，阶段 8 才拆）；
   且 `target-architecture.md §3` 把主循环职责划给 `core/PlaybackSession`。层级检查：`core → output`（`RenderFrame`）合规。
2. **5.4 方法签名简化**：计划写 `HandleSwitchRequest(PlayerState&, bool&)`，实现改为 `HandleSwitchRequest(bool& quit)`，
   `state` 经 `owner.state` 访问（会话已是 `Player` 的 friend）—— 与 5.3/5.3b 的 `bool& quit` 风格统一。
3. **5.3b 用返回枚举而非 `bool`**：`FrameAction{ Skip, Present }` 语义明确；段内 9 处 `continue` 中，
   **drain 循环内 1 处保留 `continue`**，其余 8 处 → `return FrameAction::Skip;`，末尾补 `return FrameAction::Present;`。
4. **呈现簇按值入 `MediaContext`**（而非会话级）：生命周期边界与 `ReleaseMedia` 完全吻合，无需额外钩子。

## 6. 记录的坑（规则 3：禁止凭假设）

1. **“只在代码区替换”是硬要求**：`"[Gate] render gate state="` 等日志字符串里的 `state` 会被 `\bstate\b` 误命中 →
   必须用 `codeOnly/applyCode` 逐行划分「代码区 / 字符串 / 注释」，仅在代码区做标识符改写。
2. **同名字符串之外的第二处使用**（5.2）：`osdManager->SetSubtitle(renderer, ...)` 是 **16 空格**缩进的嵌套实参
   （`RenderFrame` 的是 12 空格），按整行精确匹配会漏改 → `MSBuild error C2065: “renderer”: 未声明的标识符`。
3. **行尾差异**：`core/PlaybackSession.cpp` 是 **LF**（非 CRLF），include 锚点需同时兼容 `\r\n` 与 `\n`。
4. **多处 splice 次序**：先完成**全部断言**，再按行号**降序** splice（否则先做的 splice 会移位后续行号）。
5. **脚本自撰注释一律 ASCII**：用 latin1 写中文会被截成低字节，MSVC 报 `C1071 在注释中遇到意外的文件结束`；
   搬移来的源码行保留原 GBK 字节（latin1 裸字节读写无损）。

## 7. 后续（阶段 6~8）

- 阶段 6：`Demux`/`Sync` 抽取 + 统计（`UpdateStatistics` 198 行）收敛；处置 `legacy/` 登记项（源码引用 + 构建依赖 + 运行路径三重确认后才删）。
- 阶段 7：`Streaming`（network buffer / monitor）与 `Recording`（encoder / muxer / RTMP / HLS，≈800 行输出链）整簇外移。
- 阶段 8：反向依赖拆除收口（`Renderer/OSDManager/Event` 对 `Player.h` 的依赖；`app/Event.h` from core）+ 最终 Review，
  `Player` 收敛至 200~500 行。

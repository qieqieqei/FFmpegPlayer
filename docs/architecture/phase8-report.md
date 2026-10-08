# 阶段 8 报告：反向依赖拆除 + 收口（最终阶段）

> 计划：`phase8-plan.md`（`7cc3439`）
> 源码/文档提交范围：**`7cc3439` … `2d1fa8d`**（含本报告）
> 基线：HEAD `dd8c2db`（阶段 7 收口）；`core/Player.cpp` **1016** 行、`core/Player.h` **410** 行。
> 依据：`target-architecture.md §4/§5/§6`、`dependency.md §2/§3`、`refactor-rules.md`。

---

## 1. 结论

阶段 8 完成，三项目标全部达成：

1. **三处反向依赖全部拆除**：
   - `output/video/Renderer.cpp` → `core/Player.h`（8.1）
   - `output/osd/OSDManager.cpp` → `core/Player.h`（8.1/8.2）
   - `core/{Player,PlaybackSession}.cpp` → `app/Event.h`（8.3）
2. **UI 状态 `ControlBarState` 移出 `core`**（8.1/8.4，落在 `output/video/`，以依赖矩阵为准）。
3. **装配收口**：`Init`/`LoadConfig`/`Close`/`Run`/`MakeRenderContext` 及全部控制/工具方法的**函数体**下沉
   `PlaybackSession`，`Player` 收敛为薄门面。

- `core/Player.cpp` **1016 → 500**（−516 行，**−50.8%**，进入计划目标区间 **200~500**）
- `core/Player.h` 410 → **415**（8.1 先降 400 → 8.3/CLI 回升 → 8.6 清死 include 后 415）
- **行为零变化**：三样例 `--record` 回归 FLV 字节与时长与阶段 7 基线**逐位一致**。

---

## 2. 交付物

### 2.1 8.1（含 8.2 + 8.4）— 渲染/OSD 去 `Player*`

| 新增（纯数据） | 行数 | 用途 |
|---|---:|---|
| `output/video/RenderContext.h` | 80 | 渲染快照（SDL/sws/RGB/尺寸/状态文案/进度/时长/音量 + `OSDManager*`/`ControlBarState*`/`StatsSnapshot` + `capturePath`） |
| `output/video/ControlBarState.h` | 30 | 控制栏 UI 状态（逐字自 `core/Player.h` 搬出） |
| `output/osd/StatsSnapshot.h` | 64 | 统计快照（**纯值**；`bitrateText` 预格式化、`networkText` = `NetworkStatistics::ToString()`） |

- 填充点：`Player::MakeRenderContext`（当时在 core）→ **8.5b 后迁至 `PlaybackSession::BuildRenderContext`**。
- `Renderer.{h,cpp}` 去 `core/Player.h`；`RenderFrame(AVFrame*, const RenderContext&, bool&)`。
- `OSDManager.{h,cpp}`：`Player*` → `const StatsSnapshot&`；OSS 文本拼接逐字节不变。
- `core/Player.h`：`struct ControlBarState` → `using ControlBarState = ::ControlBarState;`（保留 `uiBar` 与 `GetControlBar()`，`app` 调用点无需改）。
- **为何三合一**：`Renderer` 去 `Player*` ⇒ `OSDManager` 必须同步（`RenderOSD` 调 `osd->Update`）；且 `Renderer` 需要 `ControlBarState` 完整类型 —— 三者互锁。

### 2.2 `--screenshot-at <sec>` CLI（`c6403a6`）

- 目的：`SendKeys` 对 SDL 窗口不可靠 → 把「视频帧 + OSD + 控制栏」的核对**全自动**。
- `Renderer` 在 `SDL_RenderPresent` **之前**用 `SDL_RenderReadPixels(..., SDL_PIXELFORMAT_RGB24, ...)` 读回渲染目标，
  自带 60 行 BMP 编码器 `SaveRenderTargetBMP`（24 位、bottom-up、行 4 字节对齐）。
  **自写 BMP 的原因**：依赖矩阵禁 `output → features`，不能借用 `features/screenshot`。
- `RenderContext.capturePath` 由 `BuildRenderContext` 在 `GetCurrentTime() >= screenshotAtSec` 时置位。
- CLI：`--screenshot-at <sec>`（`atof`）+ 可选 `--screenshot-file <path>`（默认 `screenshot_at_<sec>s.bmp`）。

### 2.3 8.3 — 输入处理改为接口注入

- 新 `core/IInputHandler.h`（19 行）：`struct IInputHandler { virtual ~IInputHandler() = default; virtual void HandleEvents(bool& quit) = 0; };`
- `Player`：`void SetInputHandler(IInputHandler*)` + 非拥有成员 `IInputHandler* inputHandler`；
  两处轮询点改 `if (inputHandler) inputHandler->HandleEvents(quit);`（`PlaybackSession` 经 `friend` 读 `owner.inputHandler`）。
- `git mv app/Event.{h,cpp} → app/EventController.{h,cpp}`；`class EventController : public IInputHandler`，
  构造注入 `Player*`，**键处理函数体逐字节不动**（原形参名 `player` 恰好成为同名成员）。
- `main.cpp`：`EventController ec(&player); player.SetInputHandler(&ec);`（在 `Run()` 前）。

### 2.4 8.5a / 8.5b / 8.5c — 装配与控制主体下沉（减重主力）

**设计（Design B）**：所有子系统 `unique_ptr` 成员**仍留在 `Player`**，只把函数**体**搬进 `PlaybackSession`，
用现成 `owner.` 前缀访问（`PlaybackSession` 是 `Player` 的 `friend`）。
→ **不动成员声明 ⇒ 零析构顺序风险、无双释放风险**，也不必改动既有 `owner.<member>` 引用（221 处）。

| 子步 | 提交 | 搬迁内容 | Player 侧 |
|---|---|---|---|
| 8.5a | `04ee5cd` | `Player::Init` → `PlaybackSession::Prepare(const char*)`；`Player::Close` → `PlaybackSession::Shutdown()` | 2–4 行门面壳；`Close` 保留 `SDL_Quit()` + `[Player] Closed` 日志 |
| 8.5b | `a7ce0aa` | `Player::Run` → `PlaybackSession::RunLoop()`；`Player::MakeRenderContext` → `PlaybackSession::BuildRenderContext(AVFrame*)` | `Run()` = `return session->RunLoop();` |
| 8.5c | `7fbbec4` | 18 个控制/工具方法体 → `PlaybackSession`（见下表） | 每个 3–6 行门面壳 |

8.5c 搬迁清单（Player 方法 → session 新名）：

| Player | session | Player | session |
|---|---|---|---|
| `LoadConfig()` | `ApplyConfig()` | `ToggleFullScreen()` | `ToggleFullScreenMode()` |
| `ToggleSubtitle()` | `ToggleSubtitleEnabled()` | `GetTimeString()` | `FormatTimeString()` |
| `TogglePause()` | `TogglePausePlayback()` | `GetDurationString()` | `FormatDurationString()` |
| `Pause()` | `PausePlayback()` | `SetCurrentTime()` | `SetPlaybackTime()` |
| `Resume()` | `ResumePlayback()` | `GetVideoWidth()` | `GetVideoWidth()` |
| `StateToString()` | `StateToString()` | `GetVideoHeight()` | `GetVideoHeight()` |
| `SetPlaybackSpeed()` | `ApplySpeed()` | `IsHardwareDecode()` | `IsHardwareDecode()` |
| `SetVolume()` | `ApplyVolume()` | `GetFramePts()` | `GetFramePts()` |
| `TakeScreenshot()` | `CaptureScreenshot()` | `SetScreenshotAt()` | `ConfigureScreenshot()` |

**保留为门面（不搬）**：4–5 行的一行式 getter / 转发（`GetDuration`/`GetState`/`GetVolume`/`GetCurrentTime`/`GetProgress`/
`GetWindow`/`GetSwsForFrame`/`GetRGB*`/`GetStatistics`/`GetNetworkStatistics`/`IsHLSActive`/`HasAudio`… 及全部 seek/frame-step/录制/HLS 转发）。

### 2.5 8.6 — 最终 Review + 清理

- `core/Player.h` 删除 5 条**死 include**（`recording/{VideoEncoder,AudioEncoder,FLVMuxer,HLSMuxer,RTMPPublisher}.h`；
  经全仓扫描确认 `core/Player.h` 是 `recording/` 之外**唯一**提到这些类的地方，且仅出现在 include 行）→ `d17e9fc`。
- 发现并修复 `ApplyConfig` 的日志文案回归（`", volume "` 被脚本误改成 `", owner.volume "`）→ `2d1fa8d`。

---

## 3. 指标（阶段 8 起 → 末）

| 文件 | 起 | 末 | 备注 |
|---|---:|---:|---|
| `core/Player.cpp` | 1016 | **500** | **−50.8%，达成 200~500** |
| `core/Player.h` | 410 | **415** | 8.1 后 400 → CLI/8.3 回升 → 清理后 415 |
| `core/PlaybackSession.cpp` | 2812 | **3674** | 承接全部下沉主体 |
| `core/PlaybackSession.h` | 209 | **252** | +43（`Prepare/Shutdown/RunLoop/BuildRenderContext` + 8.5c 18 条声明） |
| `output/video/Renderer.h` / `.cpp` | 46 / 705 | **37 / 839** | `.cpp` 增长来自 BMP 截图器 |
| `output/osd/OSDManager.h` / `.cpp` | 78 / 444 | **77 / 437** | |
| `app/main.cpp` | 304 | **344** | CLI 选项 + 注入 |
| `app/EventController.h` / `.cpp` | 24 / 368（旧 `Event.*`） | **37 / 373** | `git mv` 改名 |

新增文件 4 个：`output/video/RenderContext.h`(80)、`output/video/ControlBarState.h`(30)、
`output/osd/StatsSnapshot.h`(64)、`core/IInputHandler.h`(19)。

提交序列：`7cc3439`(计划) → `985feec`(8.1) → `c6403a6`(CLI) → `939948b`/`fddcff0`(文档) →
`01b3cf0`/`9c09e55`(8.3) → `04ee5cd`(8.5a) → `a7ce0aa`(8.5b) → `7fbbec4`(8.5c) → `d17e9fc`/`2d1fa8d`(8.6)。

---

## 4. 验证

| 项 | 方法 | 结果 |
|---|---|---|
| 构建 | MSBuild Debug/Release x64 | **0 error**；全量重建 warning 集合不变（`C4828 ×20` + `C4244 ×2`） |
| 行为回归 | 三样例 `--record` FLV 字节 | **7280913 / 9666764 / 59694920** 逐位一致 |
| 时长 | ffprobe | **12.833 / 22.655 / 141.800** s；ERROR/WARN = 0 |
| 渲染/OSD/控制栏 | `--screenshot-at 5` + 像素指标（Pillow） | osd_white **4322**（基线 4328，差 0.14%）、bar_white **476**、bar_blue **276**、bar_track_gray **10165**、bar_dark **67865**、colour_buckets **44** —— 与各子步前基线一致 |
| 装配/退出链路 | 播放 ~6 s → `CloseMainWindow()`（WM_CLOSE → SDL_QUIT） | **exit = 0**，完整收尾日志（`Threads Stopped` → `State : Stopped` → `[Main] Exit`） |
| 依赖矩阵 | 脚本扫描全部 `.h/.cpp` 的 include 边（`dependency.md §2` 的 ✗ 单元） | **129 文件 / 247 边 / 0 违规** |
| 头文件级循环 | 可达性检测 | **69 头 / 0 环** |
| `Player.h` 引用方 | 全仓扫描 | 仅 `core/{Player,PlaybackSession}.{h,cpp}` + `app/{main,EventController}.cpp`（不含 `Renderer`/`OSDManager`/`Event` 侧） |

> 脚本：`workspace/.tmp_phase86_dep.js`（矩阵扫描）、`.tmp_phase86_cycle.js`（环检测）、
> `.tmp_phase86_audit.js` / `.tmp_phase86_audit2.js`（搬移正确性）、`.tmp_bmp_px.py`（像素指标）。

---

## 5. 计划偏离与说明（照实记录）

1. **8.5 的规模算术在原计划中低估了**。§2.1 假设「`Init`+`LoadConfig`+`Close` ≈ 215 行下沉即可到 200~500」，
   实测需下沉 ≈ 866 行函数体才够（`Init` 122 + `Run` 116 + `MakeRenderContext` 173 + `Close` 48 + `LoadConfig` 54 + 控制类 ≈ 355）。
   故 8.5 拆为 **a/b/c** 三批；8.5c 收尾时补搬 `SetScreenshotAt`（第 18 个），使 `Player.cpp` **恰好落在 500**。
2. **`Close` 未加 `output.reset()`**（计划 §3.5 曾提及）。原 `Close` 本就没有，`output` 由 `~Player` 析构；
   且声明顺序已保证 `output` 先于 `configManager` 析构（逆序析构）。→ 保持行为不变的**保守**选择。
   `SDL_Quit()` 与 `[Player] Closed` 日志**保留在 `Player::Close`**（SDL 退出不属于 session 职责）。
3. **`ControlBarState` 落在 `output/video/`**，而非 `dependency.md §3.3` 草案的 `app/ControlBar.h`：
   后者会造成 `output → app` 反向依赖。**以依赖矩阵为准**（计划 §3.4 已声明）。
4. **保留下沉后的门面转发**：`app` 侧调用点零改动（用户 2026-10-09 拍板不改名 `PlayerFacade`，登记 `Player ≡ PlayerFacade`）。
5. **8.6 发现并修复一处文案回归**（`2d1fa8d`）：8.5c 的批量改名脚本把 `Logger` 字符串 `", volume "` 误改成 `", owner.volume "`。
   教训见 §6。

---

## 6. 教训（供后续阶段/项目复用）

1. **`media` 的访问方式随宿主类而变**：`Player` 持 `std::unique_ptr<MediaContext>`（`media->`），
   `PlaybackSession` 持 `MediaContext&`（`media.`）。搬移 body 时必须做 `media->` → `media.` 转换（8.5b 首版即因漏此项报 ~14 个 `C2819/C2232`）。
2. **批量标识符前缀替换必须跳过字符串字面量**。8.5c 用「非注释行全量替换」把日志文案也改了；
   正确做法是按 `"…"` 分段，仅对**字面量之外**的片段做替换（`workspace/.tmp_phase86_audit2.js` 即此口径，可复用）。
   **自查手段**：扫「字符串字面量内出现 `owner.` / `media.` / `session->`」→ 本次命中 1 处。
3. **换行符是「按文件」的**：`core/Player.cpp` / `PlaybackSession.cpp` 纯 CRLF；
   **`core/PlaybackSession.h` 是 LF 为主 + 少量 CRLF 的混合文件** → 在其上做锚点/插入必须用 `\n`。
4. **生成声明块时要按签名补 `;`**：多行签名自带 `;`，无参/单行签名若照抄方法名会漏分号（8.5c 首版 51 个 error）。
5. **完整类型成员不能用前置声明**：`Player::uiBar` 为值成员 → `Player.h` 必须 include `output/video/ControlBarState.h`（8.1 的 `C2079` 级联）。
6. **`SendKeys`/`AppActivate` 对 SDL 窗口不可靠** → 用 `--screenshot-at` + 像素指标，用 `CloseMainWindow()`(WM_CLOSE) 验证事件链路。
7. **`--record` 可能进入「自由跑」**（音频设备未及时释放/打开失败时），播放快于实时，
   录出的 FLV 会**按墙钟**变短（实测 141.8 s 的样片录成 81.5 s / 37 MB）。**两次样片之间留 ≥3 s**即可稳定复现基线。
   判定回归要以「重新单跑一次得到基线字节」为准，不能凭一次偏短的输出下结论。
8. **PowerShell 仍会咬内联 `node -e` 正则**（`$`、`(`、`|`）→ 一律写脚本文件执行。

---

## 7. 遗留与后续

- `MAX_VIDEO_PACKETS` / `MAX_AUDIO_PACKETS`（`core/Player.h` 文件级 `constexpr`）归属待重新指派（阶段性挂起项，未默认处理）。
- `core/Player.h` 中 `infra/DecodeResult.h`、`streaming/NetworkBuffer.h` 经扫描**仅出现在 include 行**，
  为**低优先**的进一步清理候选（本次只清 `recording/*` 五条，保持单一目的提交）。
- `memory/2026-10-08.md` 存在重复块，需 `read → edit → write` 去重（**勿用 `write` 直写**）。

> 阶段 8 为最终阶段，`target-architecture.md §6` 的迁移里程碑至此全部完成。

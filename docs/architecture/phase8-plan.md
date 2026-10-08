# 阶段 8 计划：反向依赖拆除 + 收口（最终阶段）

> 基线：HEAD `dd8c2db`（阶段 7 收口）；`core/Player.cpp` **1016** 行、`core/Player.h` **410** 行。
> 依据：`target-architecture.md §4/§5/§6`、`dependency.md §2/§3`（阶段 3~8 的反向依赖拆解方案在阶段 2 已定稿）。
> 硬约束沿用 `refactor-rules.md`：先审计后改；按子步推进；不改行为；不顺手修 Bug；不确定不删；触及「编译错误无法定位 / 死锁 / 破坏渲染」即**停止并报告**。

---

## 1. 阶段目标

1. **拆除三处反向依赖**（`dependency.md §1.1` 实测项）：
   - `output/video/Renderer.cpp` → `core/Player.h`（约 20 处 `player->Get*`）
   - `output/osd/OSDManager.cpp` → `core/Player.h`（约 8 处）
   - `core/{Player,PlaybackSession}.cpp` → `app/Event.h`（各 1 处 `HandleEvent`）
2. **把 UI 状态（`ControlBarState`）移出 `Player`**。
3. **装配收口**：`Init` / `LoadConfig` / `Close` 的子系统装配下沉 `PlaybackSession`，`Player` 收敛为薄门面。
4. 使 `player.cpp` 从 **1016 → 200~500**（`target-architecture.md §6` 阶段 8 指标），并出 `phase8-report.md`。

---

## 2. 只读审计（本阶段落笔前实测）

### 2.1 `core/Player.cpp` 现状（1016 行）——方法清单（按体量）

| 方法 | 行区间 | 行数 | 归属 |
|---|---|---|---|
| `Init` | 110–226 | 117 | 装配（→ 8.5 下沉 session） |
| `Run` | 233–342 | 110 | 主循环壳（保留；`Run` 已薄） |
| `LoadConfig` | 40–93 | 54 | 装配（→ 8.5） |
| `Close` | 348–391 | 44 | 装配/释放（→ 8.5） |
| `GetTimeString` | 711–745 | 35 | 时间格式化（core→features 可留） |
| `SetPlaybackSpeed` | 560–583 | 24 | 控制（留薄转发） |
| `ToggleFullScreen` / `GetDurationString` | 635–656 / 747–768 | 22 / 22 | UI/格式化 |
| `SetVolume` / `StateToString` | 590–610 / 524–543 | 21 / 20 | 控制/状态 |
| `Pause` / `Resume` / `TakeScreenshot` / `ToggleSubtitle` | 483–517 / 617–633 / 437–452 | 18 / 16 / 17 / 16 | 控制 |
| `SetCurrentTime` | 770–784 | 15 | 私有工具 |
| 其余 ≈60 个方法 | — | 各 4~11（多为薄转发/访问器） | 门面 API |

> **观察**：`Player.cpp` 已是"4 个装配大函数 + 1 个主循环 + ≈60 个一行式转发"。
> 阶段 8 的减重**几乎全部来自 8.5**（`Init`+`LoadConfig`+`Close` ≈ 215 行下沉），其余子步基本等量置换。

### 2.2 反向依赖实测

| 文件 | include 行 | 引用点 |
|---|---|---|
| `output/video/Renderer.cpp` | `:4 #include "core/Player.h"` | `GetSwsForFrame`(37) `GetRGBData`(40) `GetRGBLinesize`(43) `GetRGBTexture`(46) `GetVideoWidth`(95) `GetVideoHeight`(97) `GetState`(275,593) `GetPlaybackSpeed`(299) `GetProgress`(306,513) `FullScreenToString`(312) `GetVolume`(317) `GetTimeString`(319) `GetDurationString`(321) `GetOSDManager`(346) `GetWindow`(410) `GetControlBar`(432) `GetDuration`(510) + `Player::ControlBarState`(431) |
| `output/osd/OSDManager.cpp` | `:5 #include "core/Player.h"` | `GetStatistics`(68) `StateToString`(75) `GetTimeString`(78) `GetDurationString`(80) `IsHardwareDecode`(93) `GetNetworkStatistics`(139,143) `GetPlaybackSpeed`(151) `GetVolume`(155) |
| `core/Player.cpp` | `:4 #include "app/Event.h"` | `HandleEvent(quit, this)`(267, 主循环内) |
| `core/PlaybackSession.cpp` | `:10 #include "app/Event.h"` | `HandleEvent(...)`(2309) |
| `app/Event.cpp` | `:3 include Player.h`（**允许** app→core） | 约 25 处 `player->...` + 3 处 `Player::ControlBarState&`(226/280/344) |
| `app/main.cpp` | `:? include Player.h`（**允许**） | `Player player;`(120) 及全部调用（`LoadConfig`/`Init`/`Run`/`Close`…） |

头文件侧现状：`Renderer.h:13`、`OSDManager.h:7`、`Event.h:5` 均为 `class Player;` **前置声明**（头文件干净，问题只在 .cpp）。

### 2.3 `ControlBarState`（UI 状态）现状

- 定义：`core/Player.h`（`struct ControlBarState`，含 `visible/seekDragging/seekPreview/hoverButton/prevBtn/playBtn/nextBtn/track`）。
- 访问：`Player::GetControlBar()`（inline 返回 `uiBar` 引用）。
- 使用：`Renderer.cpp:431/432`（绘制）、`app/Event.cpp:226/280/344`（命中测试/拖动/悬停）。
- 归属：`target-architecture.md §2` 要求 → `app/ControlBar.h`。

### 2.4 依赖矩阵红线（`dependency.md §2`，本阶段必须逐步满足）

| 依赖方 | 允许依赖 | 禁止依赖 |
|---|---|---|
| `output` | `infra`, `config`, `pipeline`, `sync`（+ 同层 `output/*`，现状已用 `FontManager`/`OSDManager`） | **`hardware`/`streaming`/`recording`/`features`/`core`/`app`** |
| `features` | `infra`, `config`, `pipeline`, `sync` | `output`/`streaming`/`recording`/`core`/`app` |
| `core` | 除 `app` 外全部 | **`app`** |
| `app` | 仅 `core` | 其余全部 |

> ✅ 故 8.1（Renderer）需把 `PlayerState`、`PlayerStatistics`、`NetworkStatistics`、`ControlBarState` 等**全部降为值/自有类型**（`int`/字符串/纯 POD）。
> ❌ 现有 `OSDManager` 直接使用 `PlayerStatistics`（features）与 `NetworkStatistics`（streaming）→ 正是 8.2 要拆的点。

---

## 3. 设计

### 3.1 `Renderer` 去 `Player*`：引入 `RenderContext`（数据快照）

新建 **`output/video/RenderContext.h`**（**纯数据**，L3，不含逻辑、不 include 任何上层）：

```cpp
struct RenderContext {
    // 颜色空间转换 / RGB 目标
    SwsContext*  sws        = nullptr;
    uint8_t*     rgbData    = nullptr;
    int          rgbLinesize = 0;
    SDL_Texture* rgbTexture = nullptr;
    // 视频尺寸
    int videoWidth  = 0, videoHeight = 0;
    // 状态快照（标题 / OSD / 控制栏用）
    int          state = 0;          // PlayerState 数值（保持与现文案一致）
    const char*  stateText = "";     // StateToString()
    bool         fullscreen = false;
    const char*  fullScreenText = ""; // FullScreenToString()
    double       speed = 1.0;
    double       progress = 0.0;     // 0~1
    double       duration = 0.0;
    int          volume = 100;
    std::string  timeString, durationString;
    // 输出侧引用（数据宿主，不反向依赖）
    SDL_Window*  window = nullptr;
    OSDManager*  osd = nullptr;
    ControlBarState* bar = nullptr;  // 见 3.4
};
```

- `RenderFrame(AVFrame*, const RenderContext&) const`、`InitSDL(...)`（不变）、`UpdateWindowTitle(...)`、`RenderOSD(SDL_Renderer*, const RenderContext&)`、`RenderControlBar(SDL_Renderer*, const RenderContext&)`。
- `Renderer.h` 删除 `class Player;`，改 include `RenderContext.h`。
- **填充点**：`PlaybackSession`（渲染线程，`Run()` 内调 `RenderFrame` 处）构造 `RenderContext` 并传入 —— 这正是 `core→output` 的**向下**依赖，合规。

### 3.2 `OSDManager` 去 `Player*`：引入 `StatsSnapshot`（**纯值**统计快照）

新建 **`output/osd/StatsSnapshot.h`**（纯数据、**不含任何 L4 类型指针**）：

```cpp
struct StatsSnapshot {
    // 显性开关（等价于原 if (stats) / if (GetNetworkStatistics())）
    bool hasStats = false;
    bool hasNetwork = false;

    // PlayerStatistics 快照（全部转成值/字符串）
    std::string resolution, videoCodec, audioCodec;
    double nominalFps = 0.0, decodeFps = 0.0, renderFps = 0.0;
    std::string bitrateText;            // 预格式化：PlayerStatistics::FormatBitrate(bitrate)
    int    videoFrames = 0;
    double videoBufferMs = 0.0, audioBufferMs = 0.0;
    int    videoPackets = 0, audioPackets = 0;
    int    droppedFrames = 0;

    // NetworkStatistics 快照（直接取 ToString() 结果）
    std::string networkText;

    // 其余状态
    const char* stateText = "";        // StateToString()
    std::string timeString, durationString;
    bool   hardwareDecode = false;
    double speed = 1.0;
    int    volume = 100;
};
```

- `OSDManager::Update(SDL_Renderer*, const StatsSnapshot&)`；`OSDManager.h` 删除 `class Player;`。
- 填充点在 `PlaybackSession` 渲染路径：由 session 读 `statistics`/`networkStatistics`（core→L4 向下，合规）并**预先格式化**（含 `FormatBitrate`）。
- **为何不能直接放 `PlayerStatistics*`/`NetworkStatistics*`**：`dependency.md §2` 矩阵中 `output → features` = ✗、`output → streaming` = ✗（`output` 仅允许 `infra/config/pipeline/sync`）。故必须转成纯值。

### 3.3 `core → app` 拆除：输入处理改为**接口注入**

`app`（L6）→ `core`（L5）允许；反向禁止。故在 **core 侧定义抽象**，由 app 实现并注入：

新建 **`core/IInputHandler.h`**：

```cpp
struct IInputHandler {
    virtual ~IInputHandler() = default;
    virtual void HandleEvents(bool& quit) = 0;   // 语义 = 原 HandleEvent
};
```

- `Player` 增 `void SetInputHandler(IInputHandler*)`（非拥有）+ 成员 `IInputHandler* inputHandler = nullptr;`。
- 原 `HandleEvent(quit, player)` 调用点改为 `if (inputHandler) inputHandler->HandleEvents(quit);`。
- `app/Event.{h,cpp}` → **`app/EventController.{h,cpp}`**：提供 `class EventController : public IInputHandler`，内部持有 `Player*`（app→core 合法），把原 `HandleEvent` 体原样搬入（`HandleEvents` 里调 `player->...`）。
- `app/main.cpp`：`EventController ec(&player); player.SetInputHandler(&ec);`
- 结果：`core/*` 不再出现 `app/` include。**零行为变化**（调用序列完全相同）。

### 3.4 `ControlBarState` 迁出 `Player`

- 新建 **`output/video/ControlBarState.h`**（纯数据 struct，原字段逐字搬迁）。
- `core/Player.h`：删除 `struct ControlBarState` 与 `GetControlBar()`，删除成员 `uiBar`。
- 持有者改为 **`PlaybackSession`**（渲染所需）+ **`EventController`**（交互所需）共享同一实例：由 `Player` 持有单例并以 `ControlBarState* GetControlBar()` 暴露（`Player` 仍是门面，但**类型**已不在 core 定义 → 头文件依赖方向由 core 指向 output，合规）。
  - 备选（更彻底）：状态实例直接由 app 持有并经 `SetControlBar(ControlBarState*)` 注入，`Renderer` 从 `RenderContext.bar` 读。
- **偏离说明**：`target-architecture.md §2` 草案写"移入 `app/ControlBar.h`"，但 `Renderer`（output, L3）必须读到该类型，若定义在 app（L6）会造成 `output → app` **反向依赖**（违反 `dependency.md §2` 矩阵）。故**落在 `output/video/`**，由 app 引用（app→output 向下，合规）。**以依赖矩阵为准**。

### 3.5 装配收口（减重主力）

- `Player::Init` 的子系统装配（font/osd/sync/seek/screenshot/networkStats/buffer/streamMonitor/cuda/subtitle/playlist 的 `make_unique`）整体移入 `PlaybackSession::InitSession(...)` 或 `PlaybackSession::Prepare()`；
  `Player::Init` 变为 `session->Prepare(filename)` + 日志。
- `Player::LoadConfig` 的配置应用逻辑（speed/volume 取值）移入 session；`Player::LoadConfig` 保留为 `configManager->Load()` + 应用 + 建 `output`。
- `Player::Close` 的释放序列移入 `PlaybackSession::Shutdown()`；`Player::Close` 保 `output.reset()` / `session->Shutdown()` 顺序壳。
- 目标：`player.cpp` **1016 → 200~500**。

---

## 4. 子步设计

| 子步 | 内容 | 风险 |
|---|---|---|
| **8.1** | `output/video/RenderContext.h` + `Renderer.{h,cpp}` 去 `Player*`；`PlaybackSession` 渲染路径填充并传参 | **中**（渲染路径，需视觉核对） |
| **8.2** | `output/osd/StatsSnapshot.h` + `OSDManager.{h,cpp}` 去 `Player*` | 低-中（OSD 视觉核对） |
| **8.3** | `core/IInputHandler.h` + `Player::SetInputHandler` + 两处调用点改写；`app/Event.*` → `app/EventController.*`（`implements IInputHandler`）；`main.cpp` 注入 | 中（事件路径；键盘/鼠标回归） |
| **8.4** | `output/video/ControlBarState.h`；`Player` 去类型/`GetControlBar`；持有者调整 | 中（控制栏交互） |
| **8.5** | `Init`/`LoadConfig`/`Close` 装配下沉 `PlaybackSession` → `player.cpp` 200~500 | **中高**（生命周期/析构顺序/线程） |
| **8.6** | 最终 Review + `docs/architecture/phase8-report.md` + 计划回填 | 低 |

### 通用手法（承阶段 5/6/7 脚本）
latin1 裸字节读写；`codeOnly()` 掩码后 `{}` 配对；先断言后**降序** splice；自撰文本纯 ASCII；每步独立 commit + push；每步 Debug 0 error。
**新增铁律**（阶段 7 教训）：**在函数末尾注入代码，必须插在最后一条 `return` 之前**（插在闭合 `}` 前会成死代码 → 静默失效）。

---

## 5. 验证口径

1. 每子步 `MSBuild Debug|x64` → **0 error**；`Release|x64` → 0 error（warning 基线：Release Rebuild 43 / 增量 ~7~32，均为既有 C4828×5 + C4244×2）。
2. 回归三件套（每子步至少跑一次）：三样例 `--record` FLV 字节 **7280913 / 9666764 / 59694920**、时长 **12.833 / 22.655 / 141.800** s、ERROR/WARN = 0。
3. **⚠️ 渲染/OSD/控制栏改动（8.1/8.2/8.4）无法被 FLV 回归覆盖** → 必须另做**视觉核对**。
   ✅ **已自动化（2026-10-09，提交 `c6403a6`）**：项目新增 CLI `--screenshot-at <sec> [--screenshot-file out.bmp]` —— 播到指定时刻由 `Renderer` 在 `SDL_RenderPresent` 之前用 `SDL_RenderReadPixels` 把**合成画面**（视频 + OSD + 控制栏）读回写 24 位 BMP（默认名 `screenshot_at_<sec>s.bmp`）。
   核对方式：跑一次 `--screenshot-at 5` → 用 Python/Pillow 统计像素指标（OSD 区近白文字数、控制栏图标白像素数、进度蓝像素数、轨道灰、视频色桶数），与旧构建 / 理论值对比。
   实测（2026-10-09）：图标白像素 **476**（与真机屏抓完全相同）、OSD 近白 **4312 vs 4318**（差 0.14%）、进度蓝像素随播放位置线性（1280 宽窗口：3 s→1542、10 s→5208，理论 1577/5255）。
   （另有配套脚本：`workspace/.tmp_shot_test.ps1`、`.tmp_shot_test2.ps1`、`.tmp_bmp_px.py`；真机屏抓旧法为 `.tmp_vis81.ps1` + `.tmp_vis81_px.py`，但 `SendKeys` 对 SDL 窗口不可靠，已改由上法。）
4. 依赖验收（`dependency.md §4`）：脚本抽查 —— `output/*`、`features/*`、`pipeline/*`、`sync/*`、`infra/*` **不得**出现 `core/`（除 `core/` 自身）；`core/*` **不得**出现 `app/`；头文件级无环。
5. 每子步独立提交（源码 `refactor(...)`、文档 `docs(architecture): …`）并 `git push origin feature/live-buffer`。

---

## 6. 风险与停止条件

- **渲染路径回归不可见**：8.1/8.2/8.4 后必须真机看画面；若"能跑但不能确认画面正常" → 视为**未验证**，不得据以宣称通过。
- **8.5 生命周期风险**：`unique_ptr` 成员在 `Player` 与 `PlaybackSession` 间搬移 → 关注**析构顺序**（`output` 必须在 `configManager` 之前析构）；出现双击释放/悬垂 → **停止并报告**。
- 事件路径（8.3）若出现按键失效/重复响应 → 回滚并复核 `HandleEvents` 调用频率（应在每帧渲染循环内恰好一次）。
- 编译错误无法定位 / 死锁 / 循环依赖 → **停止并报告**。
- **命名收口待拍板**：是否把 `Player` 改名为 `PlayerFacade`（`target-architecture.md §2`）？全仓重命名噪声大、行为零收益 → **建议保留 `Player` 类名，在文档登记 `Player ≡ PlayerFacade`**，除非明确要求改名。

---

## 7. 执行记录（滚动回填）

- **8.1（含 8.2 + 8.4 合并）— 完成（`985feec`）**：`Renderer`/`OSDManager` 去 `core/Player.h`；新增 `output/video/RenderContext.h`、`output/video/ControlBarState.h`、`output/osd/StatsSnapshot.h`；`Player::MakeRenderContext(AVFrame*)` 在 core 侧组装快照；`Player::ControlBarState` 改为 `using ControlBarState = ::ControlBarState;`（`uiBar`/`GetControlBar()` 保留）。**合并原因**：`Renderer` 去 `Player*` ⇒ `OSDManager` 必须同步去 `Player*`（`RenderOSD` 调 `osd->Update`），且 `Renderer` 需要 `ControlBarState`，三者互锁。行数：`Renderer.cpp` 705→685、`OSDManager.cpp` 444→437、`Player.h` 409→400、`Player.cpp` 1015→1184（+169 = `MakeRenderContext`）。验证：Debug/Release 0 error、FLV 字节一致、**真机像素 A/B 通过**（控制栏指标完全相同：图标白 476 / 蓝 306 / 轨道灰 9700；OSD 4318 vs 4328；标题字符串逐字相同）。
- **8.2 — 已合并进 8.1**（`output/osd/StatsSnapshot.h` 纯值快照 + `OSDManager::Update(SDL_Renderer*, const StatsSnapshot&)`）。
- **8.3 …**（待填）
- **8.4 — 已合并进 8.1**（`ControlBarState` 移至 `output/video/ControlBarState.h`，不是 `app/ControlBar.h`：`app` 是 L6，`output` 引用它会形成反向依赖，**依赖矩阵优先于草稿**）。
- **8.5 …**（待填）
- **8.6 …**（待填）

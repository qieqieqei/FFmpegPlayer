# 阶段 5 执行计划：管线/呈现职责外移（Queue 路由 → VideoPresenter → Run 编排收敛）

> 起点：`core/Player.cpp` **3061 行 / 88 个方法**（`Player.h` 533 行），`PlaybackSession.cpp` 1626 行。
> 目标：继续把**管线/呈现**职责从 Player 移出，让 `Run()` 收敛为"编排骨架"。
> 硬约束（沿用 `refactor-rules.md`）：先审计后改；禁止一次性大重构；每子步 `改 → MSBuild Debug|x64 → Release|x64 → 回归 → 独立提交`；不改行为；不顺手修 Bug；不确定不删；禁止机械拆文件。

## 1. 实测现状（`phase5_scan.js`，按花括号配对统计）

| 方法 | 行数 | 归类 |
|---|---|---|
| `Run` | **645** | 渲染主循环（取帧/同步/渲染/OSD/统计 + switch/reconnect/buffer-gate 编排）|
| `UpdateStatistics` | 198 | 统计采样 |
| `Init` | 117 | 装配 |
| `StartHLS` / `StartPushing` / `StartRecording` | 117 / 96 / 71 | 输出链 |
| `EnsureOutEncoders` / `FeedOutputVideo` / `ToYuv420p` / `FeedOutputAudio` / `FlushOutEncoders` / `StopAllOutputs` / `DispatchVideoPacket` / `ReleaseOutEncoders` | 112 / 73 / 71 / 58 / 63 / 52 / 50 / 28 | 输出链（≈**800 行**，阶段 7 整簇外移）|
| `ExpandPlaylistWithSiblings` | 106 | features/playlist |
| `GetSwsForFrame` | 50 | **呈现**（SDL/sws/RGB）|
| `PushVideoPacket` / `PushAudioPacket` | 34 / 31 | **队列路由** |
| 各 `Is*/Get*Queue*` | 4–6 ×6 | 队列路由 |
| 20+ 个 4 行访问器 | ≈90 | Facade API（**保留**）|

结论：阶段 5 的可搬对象 = **队列路由（≈110 行）+ 呈现簇（≈150–250 行）+ `Run()` 内的编排块（≈250 行）**。输出链与统计留到阶段 7 / 阶段 6。

## 2. 子步

| 步 | 内容 | 风险 | 验证 |
|---|---|---|---|
| **5.1** | 队列路由外移：`PushVideoPacket/PushAudioPacket/PopVideoPacket/PopAudioPacket/IsVideoQueueInterrupted/IsAudioQueueInterrupted/GetVideoQueueSize/GetAudioQueueSize/GetVideoQueueCapacity` + 丢包锚点 `lastVideoDropped/lastAudioDropped` → **`PlaybackSession`**（Demux/解码线程的宿主）；Player 侧仅 `session->Get*QueueSize()` 供统计 | 低 | 编译+回归；调用点核对 |
| **5.2** | 呈现簇 → 新 `output/video/VideoPresenter`：SDL 资源（`window/renderer/texture/rgbTexture`）+ `swsCtx/swsSrc*` + `rgbData/rgbLinesize` + `lastFrame` + 方法 `GetSwsForFrame/TakeScreenshot/GetWindow/GetRGBData/GetRGBLinesize/GetRGBTexture` | 中 | 编译+回归（GUI 路径经 `--record` 跑通）|
| **5.3** | `Run()` 内"取帧 → 音视频同步 → 丢帧 → 渲染 → 统计/时间"段 → `VideoPresenter` 的呈现方法（Run 只保留循环与状态机） | 中高 | 编译+回归（时长/丢帧计数一致）|
| **5.4** | `Run()` 内 switch/reconnect 编排（≈250 行）→ `PlaybackSession::HandleSwitchRequest(PlayerState&, bool& quit)` / `HandleReconnect(...)` | 中 | 编译+回归（本地无断网路径，靠代码等价性 + 回归无退化）|
| **5.5** | 阶段提交 + `docs/architecture/phase5-report.md` | — | Debug+Release + 全回归 |

> 阶段 5 结束后 `Player.cpp` 预期 **≈2400–2600 行**；`Run()` 预计 645 → ≈300–350 行。剩余大头（输出链 ≈800、统计 198、`Init` 117）分别属阶段 7 / 6。

## 3. 设计要点（避免循环依赖）

- **依赖方向**（`dependency.md` 6 层）：`core` → `output` 允许；`output` **不得** → `core`（阶段 8 拆反向）。
  因此 `VideoPresenter` 只接**数据**，不接 `Player*`：呈现所需的 pts/帧/状态通过参数或 `RenderContext` 快照传入。
- **`PlaybackSession` 继续持 `MediaContext&` + `Player&`**（阶段 4 定案），队列路由移入后仍是 `media.` 访问。
- 新增类只做**真实多调用点**的抽取；不建空壳接口。

## 4. 停止并报告的条件（沿用）

编译错误无法定位 / 死锁 / join 卡死 / double-free / 长稳恶化 / 行为不可判定 → **立即停止并报告**。

## 5. 回归口径（沿用阶段 4）

`x64\Release\FFmpeg_text_claw.exe --record <sample> --log-file x.log`（`--record` 置 autoQuitOnEof），
样例 `124662f108eca04d9189d0efae3829c7.mp4`(12.833s) / `21e1626495c5d9174868eebab99e437c.mp4`(22.655s) / `a4c277.mp4`(141.8s)；
判定：exit 0 + FLV 时长逐位一致 + 无 ERROR/WARN。构建 `Debug|x64` 与 `Release|x64` 均 0 error。

## 6. 执行记录（滚动更新）

### 5.1 队列路由外移 —— 完成（提交 `待填`）

**搬移清单**（9 方法 + 2 锚点，`Player` → `PlaybackSession`）：

- 方法：`PushVideoPacket` / `PushAudioPacket` / `PopVideoPacket` / `PopAudioPacket` / `IsVideoQueueInterrupted` / `IsAudioQueueInterrupted` / `GetVideoQueueSize` / `GetAudioQueueSize` / `GetVideoQueueCapacity`
- 成员：`lastVideoDropped` / `lastAudioDropped`
- `PlaybackSession` 内一律 `media.xxx`（引用，非指针）；丢包统计经 `owner.networkStatistics`（`PlaybackSession` 是 `Player` 的 friend，阶段 4 已建立）
- `PlaybackSession.cpp` 内原 `owner.Push/Pop*` / `owner.Is*Interrupted` 调用改回直接调用（无转发层）
- `Player` 侧仅 `Player::UpdateStatistics` 有 3 个调用点（1653 附近 `GetVideoQueueSize()×3 / GetAudioQueueSize()×1 / GetVideoQueueCapacity()×1`）→ 改前缀 `session->`

**实测 diff**：`Player.h -32`（含段注释）、`Player.cpp -144`、`PlaybackSession.h +30`、`PlaybackSession.cpp +149`（9 个定义全部 ASCII 注释）。

**未动（有意）**：`MAX_VIDEO_PACKETS=120 / MAX_AUDIO_PACKETS=60 / MAX_VIDEO_FRAMES=12` 仍在 `core/Player.h:84-88` 文件作用域；其中 `MAX_VIDEO_FRAMES` 早已被 `PlaybackSession.cpp` 使用（经 `Player.h` 传递可见）。队列容量常量归 `pipeline/` 的清理属阶段 8 收口，避免本子步扩大范围。

**验证**：`MSBuild Debug|x64` = 0 error / 32 warning（与 4.4 基线一致，无新增）；`Release|x64` = 0 error / 32 warning；三样例 `--record` 全部 exit 0，FLV 时长 **12.833 / 22.655 / 141.8 s**（与 4.4 逐位一致），日志 ERROR/WARN = 0，尾行 `Threads Stopped → State : Stopped → All outputs stopped → Closed ×2`（两条 Closed 为既有 `Close()` 无幂等守卫所致，非本子步引入）。

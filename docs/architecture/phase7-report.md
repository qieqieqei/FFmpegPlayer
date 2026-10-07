# 阶段 7 报告：输出链外移（OutputPipeline）

> 计划：`phase7-plan.md`（提交 `9228c15`）
> 源码提交：**`123d238`** `refactor(recording): extract output chain into OutputPipeline (phase 7.1)`（已推送 `9228c15..123d238`）
> 基线：HEAD `625b05c`（阶段 6 收口）；`core/Player.cpp` **1955** 行、`core/Player.h` **473** 行。

---

## 1. 结论

阶段 7 完成。`Player` 中体量最大的**输出链**（编码器装配 / 帧注入 / 编码包分发 / 录制 / 推流 / HLS）整体外移为
`recording/OutputPipeline.{h,cpp}`（L4），`Player` 只保留 12 个公共薄转发与装配代码。

- `core/Player.cpp` **1955 → 1016**（−939 行，**进入阶段 7 目标区间 800~1300**）
- 新增 `recording/OutputPipeline.h` **142** 行 / `recording/OutputPipeline.cpp` **1027** 行
- **行为零变化**：三样例 `--record` 回归 FLV 字节与时长与阶段 6 基线**逐位一致**。

---

## 2. 交付物

### 2.1 新类 `recording/OutputPipeline`

| 项 | 内容 |
|---|---|
| 层 | L4 `recording`（可依赖 `recording/*` 同级、`Config/*` L0、`infra/*` L0、ffmpeg） |
| 持有成员（15） | `outMutex`、`outVideoEncoder`、`outAudioEncoder`、`recordMuxer`、`rtmpPublisher`、`hlsMuxer`、`outSws`、`outYuvFrame`、`outVideoPts`、`outAudioPts`、`outVideoPktIdx`、`outAudioPktIdx`、`recording`、`pushing`、`hlsActive` |
| 公共方法 | `SetSourceInfo`、`FeedOutputVideo`、`FeedOutputAudio`、`StartRecording/StopRecording/ToggleRecording/IsRecording`、`StartPushing/StopPushing/TogglePushing/IsPushing`、`StartHLS/StopHLS/ToggleHLS/IsHLSActive`、`StopAllOutputs`、`ReleaseOutEncoders` |
| 私有方法 | `EnsureOutEncoders`、`ToYuv420p`、`DispatchVideoPacket`、`DispatchAudioPacket`、`FlushOutEncoders` |
| 构造 | `explicit OutputPipeline(ConfigManager* config)`（非拥有，构造注入；`config` 仅在 `EnsureOutEncoders`/`StartPushing`/`StartHLS` 解引用） |
| 析构 | 头中声明、`.cpp` 中 `= default`（pimpl 式，避免在头里要求编码器完整类型；行为 = 成员随对象析构，与原先一致） |
| 文件静态函数 | `CopyCodecPar(AVCodecContext*)` —— 自 `Player.cpp` 顶层**一并迁入**（仅输出链使用） |

### 2.2 `SourceInfo`（把 `media` 从输出链里彻底摘掉）

```cpp
struct SourceInfo {
    bool hasVideo = false;
    int  width = 0, height = 0;
    int  fps = 25;
    bool hasAudio = false;
    int  sampleRate = 48000;
    int  channels = 2;
};
```

由 `PlaybackSession::OpenMedia` 在**函数末尾、`return true;` 之前**调用 `owner.output->SetSourceInfo(info)` 注入：
`hasVideo ← media.videoDecoder != nullptr`、`width/height ← media.videoDecoder->GetContext()`、
`fps ← 1.0 / media.videoFrameDuration + 0.5`（≤0 时回落 25）、
`hasAudio/sampleRate/channels ← media.demuxer->GetAudioStream()->codecpar`（`sample_rate≤0→48000`、`nb_channels≤0→2`）。

`EnsureOutEncoders()` 以 `srcInfo` 取代原 `media->videoDecoder->GetContext()` / `media->videoFrameDuration` / `media->demuxer->GetAudioStream()`；
`!srcInfo.hasVideo` 时直接 `return false`，与原「无视频解码器则无法装配」语义等价。

### 2.3 `Player` 侧

- 成员：删除 15 个输出链成员；新增 `std::unique_ptr<OutputPipeline> output;`（声明在 `configManager` **之后** → 析构逆序安全）。
- 创建：`LoadConfig()` 中 `configManager` 创建之后 `if (!output) output = std::make_unique<OutputPipeline>(configManager.get());`。
  （`LoadConfig` 早于 `Init`→`OpenMedia`，保证 `OpenMedia` 注入 `SourceInfo` 时 `output` 已存在。）
- 保留 **12 个公共薄转发**：`StartRecording/StopRecording/ToggleRecording`、`StartPushing/StopPushing/TogglePushing`、
  `StartHLS/StopHLS/ToggleHLS`、`IsRecording/IsPushing/IsHLSActive`（`app/Event.cpp`、`app/main.cpp` 调用点不变）。
- `Player.h` 私有声明块与成员块整体删除；文件顶部新增 `#include "recording/OutputPipeline.h"`。

### 2.4 `PlaybackSession` 侧

- `owner.FeedOutputVideo(...)` ×2 → `owner.output->FeedOutputVideo(...)`；
- `owner.FeedOutputAudio(...)` ×1 → `owner.output->FeedOutputAudio(...)`；
- `owner.StopAllOutputs()` ×1（`ReleaseMedia`） → `owner.output->StopAllOutputs()`；
- `OpenMedia` 末尾注入 `SetSourceInfo`。

### 2.5 工程登记

- `FFmpeg_text_claw.vcxproj`：`ClCompile` 增 `recording\OutputPipeline.cpp`、`ClInclude` 增 `recording\OutputPipeline.h`（插在 `RTMPPublisher` 之后）。
- `.vcxproj.filters`：**未登记** —— 依既有先例（`RTMPPublisher.*` 亦不在 filters），MSBuild 不要求；避免臆造过滤器名。

---

## 3. 指标

| 文件 | 阶段 6 末 | 阶段 7 末 | Δ |
|---|---|---|---|
| `core/Player.cpp` | 1955 | **1016** | −939 |
| `core/Player.h` | 473 | **410** | −63 |
| `core/PlaybackSession.cpp` | 2756 | **2812** | +56 |
| `core/PlaybackSession.h` | 208 | **209** | +1 |
| `recording/OutputPipeline.h` | — | **142** | 新增 |
| `recording/OutputPipeline.cpp` | — | **1027** | 新增 |
| `FFmpeg_text_claw.vcxproj` | 104 | **106** 条目 | +2 |

> `Player.cpp` 剩余 ≈1016 行 ≈ `Init`/`Close`/`LoadConfig` 装配 + `Run()` 主循环壳 + UI 访问器 → 交**阶段 8** 收口。

---

## 4. 验证

| 项 | 结果 |
|---|---|
| `MSBuild Debug\|x64` | **exit 0 / 0 error**，无新增 warning（增量 7 条全为既有 C4828×5 + C4244×2） |
| `MSBuild Release\|x64` | **exit 0 / 0 error**，同上 |
| 样例 1 `124662f…mp4` | exit 0；FLV **7280913** B、**12.833** s（与基线逐位一致）；ERROR/WARN = 0 |
| 样例 2 `21e1626…mp4` | exit 0；FLV **9666764** B、**22.655** s（一致）；ERROR/WARN = 0 |
| 样例 3 `a4c277.mp4` | exit 0；FLV **59694920** B、**141.800** s（一致）；ERROR/WARN = 0 |

> 回归命令（cwd `x64\Release`）：`FFmpeg_text_claw.exe --record ..\..\vidio101\<sample> --log-file reg_<sample>.log`；
> 时长用 `ffprobe -show_entries format=duration` 复核。

---

## 5. 本轮踩坑（三条，均可复用）

### 5.1 ⚠️ 「插入到函数末尾」必须插在 **最后一条 `return` 之前**，不是闭合 `}` 之前
`OpenMedia` 末尾是 `... ; return true; }`。首版脚本把 `SetSourceInfo` 注入块放在**闭合 `}` 之前**，即落在 `return true;` **之后** → 成为**死代码**；
编译器**不报错也不告警**，程序照常播放，但 `srcInfo` 永远为空 → `EnsureOutEncoders()` 因 `!hasVideo` 直接 `return false` →
**录制静默不启动**（无任何日志、exit 仍 0、也没有 `.flv`）。
- **教训**：这类「不可达注入」是**最难发现**的失败模式（编译通过、运行不崩、只少一个产物）。
- **修法**：定位函数闭合 `}` 后，从 `}` 向上找**第一条以 `return` 开头的代码行**，把注入块插在它**之前**。
- **检测**：回归不要只看 exit code，必须**核对产物字节/存在性**。

### 5.2 文件级 `static` 辅助函数要跟着调用者一起走
`static AVCodecParameters* CopyCodecPar(...)` 原在 `Player.cpp` 顶层（非成员），仅被输出链的 `Start*` 使用。
只搬方法体 → `error C3861: "CopyCodecPar" 找不到标识符` ×6。**修法**：把该静态函数连带其注释一并迁入 `OutputPipeline.cpp`（置于首次使用之前）。
（若留在 `Player.cpp` 会变成未引用静态函数 → 触发新告警，故必须删。）

### 5.3 单行方法定义生成的转发会坏
若某方法是**单行定义**（`bool Player::IsRecording() const { return recording; }`），
转发替换若取 `sig = lines[s0..b0]`（`b0` = 含 `{` 的行）会**把原方法体一起留在签名里**。
**修法**：`b0 === s0` 时，签名取该行 `{` 之前的文本 + `" {"`，再拼 `[forwarder, "}"]`。

---

## 6. 后续（阶段 8）

1. 反向依赖收口：`core → app` 反依赖（`Player.h` include `app/Event.h`、`Renderer`/`Event` 回指 `Player*`）与 `Player` 的 UI 访问器壳收敛。
2. `Player.cpp` 目标 **200~500** 行：消化 `Init`/`Close`/`LoadConfig` 装配（≈）与 `Run()` 主循环壳。
3. 清理阶段 5/6/7 遗留：`Player.h` 中已不再需要的 `recording/*` 具体编码器 include、`MAX_VIDEO_PACKETS/MAX_AUDIO_PACKETS` 归属等登记项。

# 重构锁定规则（阶段 1 → 阶段 2 启动前）

> 本文件锁定进入阶段 2 之前必须固定的口径，供后续所有大阶段遵守。
> 基线 commit：`2af4d01`　基线 tag：`architecture-audit-baseline-2af4d01`

---

## 1. Git 基线规则

- 基线 commit：`2af4d01`（`feature/live-buffer`）。
- 阶段 1 审计文档**单独提交**，不与后续源码重构混合。
- **判定"是否属于架构重构改动"的唯一口径**：

  ```bat
  git diff 2af4d01            :: 工作树 vs 基线（应只剩 docs/ 文档差异）
  git diff 2af4d01 --stat     :: 概览
  ```

- 每个大阶段**单独 commit**，不压成一个；阶段产出的文档（`docs/architecture/*.md`）随该阶段提交。
- 提交前必须：`git status` 干净（或仅剩预期改动）→ 再 commit。

---

## 2. MSBuild 编译口径（锁定）

- `MSBuild` **不在 PATH**（`where MSBuild` 无结果）。必须使用完整路径。
- 唯一可用 MSBuild：

  ```text
  D:\application\visual studio\IDE\MSBuild\Current\Bin\MSBuild.exe
  ```

- 工程文件（实测位置）：
  - `D:\application\visual studio\product\FFmpeg_text_claw\FFmpeg_text_claw.sln`
  - `D:\application\visual studio\product\FFmpeg_text_claw\FFmpeg_text_claw.vcxproj`
- 实测 Configuration/Platform（来自 `.vcxproj`，**非猜测**）：

  ```text
  Debug|Win32   Release|Win32   Debug|x64   Release|x64
  ```

- 工具集/标准：`PlatformToolset=v143`，`LanguageStandard=stdcpp17`，`WindowsTargetPlatformVersion=10.0`，`ConfigurationType=Application`。
- 第三方依赖：Include `D:\FFmpeg\include`、`D:\SDL2\include`、`D:\library\SDL2_tff\SDL2_ttf-2.24.0\include`；Lib `avcodec/avformat/avutil/swscale/swresample/avfilter/SDL2/SDL2main/SDL2_ttf`。

### 固定命令（在项目根目录执行）

```bat
:: 高风险迁移：每步编译（Debug）
"D:\application\visual studio\IDE\MSBuild\Current\Bin\MSBuild.exe" FFmpeg_text_claw.vcxproj /p:Configuration=Debug /p:Platform=x64 /m /v:minimal

:: 每个大阶段完成后：Release
"D:\application\visual studio\IDE\MSBuild\Current\Bin\MSBuild.exe" FFmpeg_text_claw.vcxproj /p:Configuration=Release /p:Platform=x64 /m /v:minimal
```

### 验证节奏（强制）

```text
修改
 ↓
MSBuild Debug /p:Platform=x64
 ↓
运行 / 针对性测试
 ↓
通过后才继续下一步
```

每个大阶段收尾：

```text
MSBuild Release /p:Platform=x64
```

> 说明：`/v:minimal` 便于抓 `error/warning`；如判定构建结果，**输出先落文件再 grep**（避免管道提前掐断导致伪 `-1`）。

---

## 3. Player.cpp 行数目标规则（锁定）

- 现状：`Player.cpp` ≈ **4651 行**（含空行；非空约 3649）。
- 最终目标：**约 200~500 行**。

### 3.1 禁止事项

**禁止为达到行数目标而机械拆文件。** 以下行为**判定重构失败**：

```text
Player.cpp
  ↓ 只是把原有逻辑搬走
PlayerHelper.cpp  PlayerUtils.cpp  PlayerManager.cpp  PlayerController.cpp
```

即：仅把原 Player 逻辑物理搬运到各种 `Helper/Utils/Manager`，职责并未重新划分。

### 3.2 Player 最终**只允许**负责

```text
对外播放器 API
播放会话生命周期
高层状态
模块协调（协调者，而非实现者）
```

### 3.3 Player 最终**不得继续**承担

```text
Demux    Video Decode    Audio Decode    Packet Queue    Frame Queue
PCM Queue    A/V Sync    Renderer    Audio Device    Network Buffer
Encoding    Muxing    RTMP    HLS    Screenshot    Statistics
Config Parsing
```

### 3.4 渐进式目标（参考，非机械验收阈值）

```text
4651
 ↓ 抽 PlaybackSession
3000~3500
 ↓ 抽 VideoPipeline
2200~2800
 ↓ 抽 AudioPipeline
1500~2000
 ↓ 抽 Demux / Seek / Sync
800~1300
 ↓ 抽 Streaming / Recording / Statistics / Screenshot
300~600
 ↓ 最终架构 Review
```

### 3.5 判定口径

- 若最终 Player 仍**略高于 500 行**，但**职责已完全收敛**（仅剩 §3.2 允许项），须在最终报告中**解释原因**。
- 若 Player **小于 500 行**仅仅因为把原逻辑机械搬进 Helper/Utils/Manager，则**判定重构失败**（见 §3.1）。

---

## 4. 通用硬规则（承自计划书）

- 先审计后改；禁止一次性大重构；每步"编译→运行→回归→确认"。
- 保持现有功能行为不变；不顺手修无关 Bug；不删除无法确认的代码。
- 不把现有代码全改智能指针；不隐藏编译错误；不滥建接口为抽象而抽象。
- 不许两个执行者同时改同一工作区。
- 出现以下任一：编译错误无法定位 / 死锁 / join 卡死 / double-free / 长稳恶化 / Legacy 不确定 → **停止并报告**。

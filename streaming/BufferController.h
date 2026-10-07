#pragma once

// ============================================================
// BufferController - 网络播放缓冲状态机（2026-08-16 v2 重写）
//
// 背景（7.3 旧版问题，已确认）：
//   旧版是"即时比较"：NeedBuffer() = bufferedMs < lowWater，
//   Update() 只在进入时产生一次性事件，无锁存、无去抖。
//   → 水位在 lowWater 附近振荡时，门控 ~1s 周期 toggle，
//     播放端反复 pause/resume（上次实验失败根因之一）。
//
// v2 改为真正的状态机（锁存 + 去抖）：
//
//   STOPPED
//     │ Start()（打开直播媒体成功）
//     ↓
//   PREBUFFERING ── buffer >= highWater ──→ PLAYING
//     ↑                                          │
//     │                                          │ buffer < lowWater
//     │                                          │ 持续 ≥ debounceMs
//     │                                          ↓
//     │                                       REBUFFERING
//     │                                          │ buffer >= highWater
//     │                                          ↓（一次性放行，锁存）
//     └────────────── PLAYING ◄──────────────────┘
//
//   PLAYING / REBUFFERING ── 超过 stallTimeoutMs 无新包 ──→ STALLED
//   STALLED ── 恢复收包 ──→ PREBUFFERING
//
// 关键语义：
//   - 锁存（hysteresis）：进入 REBUFFERING 后，只有 buffer >= highWater
//     才恢复 PLAYING；中途 800/900/700/1100 波动一律不动作
//   - 去抖：buffer < lowWater 必须持续 ≥ debounceMs 才进入 REBUFFERING
//   - 用户 Pause 与网络 Buffering 是两个独立状态（PAUSED 归 Player 管）
//   - 一次性状态事件：ConsumeEnterBufferingEvent / ConsumeExitBufferingEvent
//     供 Player 在状态跳变时各响应一次（不每帧 toggle）
//
// 线程归属：
//   Update() / Consume*()      ：渲染线程独占
//   OnPacketReceived()         ：Demux 线程（原子安全）
// ============================================================

#include <atomic>
#include <cstdint>

// 缓冲状态机状态
enum class BufferState
{
    Stopped,        // 未开始 / 已 Reset
    Prebuffering,   // 预缓冲：启动或断流恢复后攒水
    Playing,        // 正常播放
    Rebuffering,    // 播放中缓冲不足（已锁存）
    Stalled         // 长时间无新数据（断流）
};

class BufferController
{
public:

    BufferController();

    // ---------- 模式 / 参数 ----------

    // 兼容旧接口：设置播放类型（直播 / 点播）
    // live = true  -> 直播（默认稳定缓冲模式）
    // live = false -> 点播（不参与网络缓冲状态机，恒 Playing）
    void SetLive(
        bool live);

    bool IsLive() const;

    // 缓冲模式（直播）：
    //   stable      : lowWater=750 / target=1500 / highWater=1950 / max=3000
    //                 去抖 300ms，stall 超时 5000ms（抗抖动，目标延迟 1~2s）
    //   low_latency : lowWater=150 / target=300 / highWater=400 / max=500
    //                 去抖 0ms（保持旧版低延迟行为，目标 ≈300ms）
    void SetMode(
        bool stable);

    // 自定义目标缓冲（毫秒）——CLI --live-buffer N 用。
    // 水位自动缩放：low = N/2，high = N*1.3，max = N*2
    void SetTargetBufferMs(
        int ms);

    // 进入 REBUFFERING 的去抖时长（毫秒）
    void SetDebounceMs(
        int ms);

    // 断流判定超时（毫秒）：超过该时长没有收到新包 → STALLED
    void SetStallTimeoutMs(
        int ms);

    // ---------- 运行 ----------

    // 每帧更新当前缓冲时长（毫秒），内部结算状态机（渲染线程）
    void Update(
        double bufferedMs);

    // Demux 线程每收到一个包调用（刷新断流计时）
    void OnPacketReceived();

    // 重置（Seek / 切歌 / 打开媒体时调用）→ Stopped
    void Reset();

    // 打开直播媒体成功后调用 → Prebuffering（启动预缓冲）
    void Start();

    // ---------- 查询 ----------

    BufferState GetState() const;

    const char* GetStateName() const;

    // 是否应暂停消费（解码线程停取包）：
    // Prebuffering / Rebuffering / Stalled → true
    bool IsConsumingBlocked() const;

    double GetBufferedMs() const;

    int GetTargetMs() const;

    int GetLowWaterMs() const;

    int GetHighWaterMs() const;

    int GetMaxBufferMs() const;

    // ---------- 统计（Metrics，每秒日志用） ----------

    // 断流次数（进入 STALLED）
    int GetStallCount() const;

    // 欠载次数（进入 REBUFFERING）
    int GetUnderrunCount() const;

    // 进入缓冲类状态总次数（REBUFFERING + STALLED 入场）
    int GetBufferingCount() const;

    // 累计处于缓冲类状态时长（毫秒，含启动预缓冲）
    int64_t GetBufferingDurationMs() const;

    // ---------- 兼容旧接口 ----------

    // 缓冲不足（旧语义）：处于缓冲类状态
    bool NeedBuffer() const;

    // 缓冲充足（旧语义）：正在正常播放
    bool IsEnough() const;

    // 是否刚进入"缓冲中"（含断流），一次性查询（旧 OSD 用）
    bool ConsumeBufferingEvent();

    // ---------- 新事件（Player 状态跳变时一次性响应） ----------

    // 进入缓冲（Playing → Rebuffering / Stalled）一次性事件
    // Player 响应：audioDevice->SetPaused(true) + 关时长修剪
    bool ConsumeEnterBufferingEvent();

    // 退出缓冲（→ Playing）一次性事件
    // Player 响应：audioDevice->SetPaused(false) + 恢复时长修剪
    bool ConsumeExitBufferingEvent();

private:

    void ApplyModeWatermarks();

    // ---------- 模式 ----------

    bool live = true;            // 是否直播

    bool stable = true;          // 稳定缓冲模式（true）/ 低延迟（false）

    bool modeExplicit = false;   // 是否显式 SetMode 过（SetLive 不再覆盖）

    // ---------- 水位（毫秒） ----------

    int lowWaterMs = 750;        // 进入 REBUFFERING 阈值

    int targetWaterMs = 1500;    // 目标水位（统计/调速用，不做门限）

    int highWaterMs = 1950;      // 恢复 PLAYING 阈值（锁存）

    int maxBufferMs = 3000;      // 绝对上限（统计 / NetworkBuffer 时长阈值来源）

    int debounceMs = 300;        // 进入 REBUFFERING 去抖

    int stallTimeoutMs = 5000;   // 断流超时

    // ---------- 状态 ----------

    BufferState state = BufferState::Stopped;

    std::atomic<int64_t> lastPacketTick{ 0 };   // 最近收包时刻（毫秒，steady）

    std::atomic<int64_t> lowWaterSince{ -1 };   // 连续低于 lowWater 的起始时刻

    std::atomic<double> bufferedMs{ 0.0 };      // 最近一次 Update 的缓冲时长

    // ---------- 事件（一次性，原子） ----------

    std::atomic<bool> bufferingEvent{ false };  // 进入缓冲类状态（兼容 OSD）

    std::atomic<bool> enterEvent{ false };      // 进入 Rebuffering / Stalled

    std::atomic<bool> exitEvent{ false };       // 恢复 Playing

    // ---------- 统计（Metrics） ----------

    std::atomic<int> stallCount{ 0 };           // 断流次数

    std::atomic<int> underrunCount{ 0 };        // 欠载次数（进 REBUFFERING）

    std::atomic<int> bufferingCount{ 0 };       // 进缓冲类状态次数

    std::atomic<int64_t> bufferingDurationMs{ 0 };  // 缓冲累计时长

    std::atomic<int64_t> bufferingStartTick{ -1 };  // 缓冲开始时刻（-1 = 未在缓冲）

    // 缓冲类状态时长累计开始 / 结束（渲染线程）
    void EnterBuffering();

    void LeaveBuffering();
};

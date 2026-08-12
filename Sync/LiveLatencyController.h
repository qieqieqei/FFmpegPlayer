#pragma once

// ============================================================
// LiveLatencyController - 直播延迟追帧控制器（9.0）
//
// 背景（评审意见）：
//   旧版"直播追帧"只是 LiveClock 基于 A/V clock 偏差的
//   阈值丢帧（delay > +ahead / < -behind 就丢），
//   不是真正的"智能追帧"。
//
// 本类把直播延迟控制独立出来，综合：
//   当前直播延迟（估算端到端）
//   网络抖动（jitter）
//   缓冲区长度（bufferMs）
//   历史卡顿 / A/V 漂移（audioVideoDiffMs）
//   决定追帧策略。
//
// 三级追帧策略（替代"只会丢帧"）：
//
//   正常      latency < target            -> 1.00x，不丢帧
//   轻度追帧  latency 超阈值（轻）        -> 1.03x 变速追赶（音频变速不变调）
//   中度追帧  latency 超阈值（中）        -> 1.08x 变速追赶
//   重度追帧  latency 超阈值（重）        -> 1.15x 变速 + 丢帧
//   严重追帧  latency 超阈值（严重）      -> aggressive 倍速 + 持续丢帧
//
//   例（默认阈值，全部可配置）：
//     latency <  200ms  -> 1.00x
//     200 ~ 350ms       -> 1.03x
//     350 ~ 500ms       -> 1.08x
//     500 ~ 1000ms      -> 1.15x + Drop
//     > 1000ms          -> aggressive chase
//
// 细节：
//   - 档位判定带滞回（hysteresis）：进入高档后要回落更多才降档，
//     避免临界抖动导致档位反复横跳
//   - 追帧倍速平滑变化（maxChaseStep）：每次 Update 最多移动一小步，
//     音频变速无感知（SOLA 平滑拼接）
//   - 丢帧带防抖：连续 N 次建议 + 冷却时间，避免单帧尖峰误丢
//
// 线程归属：渲染线程调用 Update / 查询；Reset 任意线程。
// 内部互斥锁保护（与 NetworkStatistics 风格一致）。
// ============================================================

#include <mutex>
#include <chrono>

class LiveLatencyController
{
public:

    LiveLatencyController();

    // ---------- 目标 / 边界 ----------

    // 目标直播延迟（毫秒）。latency < target 视为正常，不追帧。
    void SetTargetLatencyMs(
        double latencyMs);

    double GetTargetLatencyMs() const;

    // 最低 / 最高允许延迟（毫秒，边界保护）
    void SetMinLatencyMs(
        double latencyMs);

    void SetMaxLatencyMs(
        double latencyMs);

    // ---------- 追帧档位配置 ----------

    // 各档位追帧倍速（默认 1.03 / 1.08 / 1.15 / 1.25）
    void SetChaseSpeedLight(
        double speed);

    void SetChaseSpeedMedium(
        double speed);

    void SetChaseSpeedHeavy(
        double speed);

    void SetChaseSpeedAggressive(
        double speed);

    // 各档位触发阈值（毫秒，默认 200 / 350 / 500 / 1000）
    void SetChaseThresholdLightMs(
        double ms);

    void SetChaseThresholdMediumMs(
        double ms);

    void SetChaseThresholdHeavyMs(
        double ms);

    void SetChaseThresholdAggressiveMs(
        double ms);

    // ---------- 平滑 / 防抖 ----------

    // 追帧倍速每次 Update 的最大变化量（默认 0.02）
    void SetMaxChaseStep(
        double step);

    // 降档滞回（毫秒，默认 50）：error 低于 当前档位阈值-滞回 才降档
    void SetHysteresisMs(
        double ms);

    // 丢帧冷却（毫秒，默认 150）与连续建议次数（默认 3）
    void SetDropCooldownMs(
        double ms);

    void SetDropConsecutive(
        int count);

    // ---------- 每帧更新 ----------

    // 综合更新（渲染线程每帧或节流调用）：
    //   liveLatencyMs    : 当前直播延迟估算（端到端，毫秒）
    //   bufferMs         : 缓冲区长度（毫秒）
    //   jitterMs         : 网络抖动（毫秒）
    //   audioVideoDiffMs : A/V 时钟偏差（视频-音频，毫秒）
    void Update(
        double liveLatencyMs,
        double bufferMs,
        double jitterMs,
        double audioVideoDiffMs);

    // ---------- 决策输出 ----------

    // 是否需要追帧（当前追帧倍速 != 1.0）
    bool ShouldChase() const;

    // 是否需要丢帧追赶（重度/严重档，防抖后判定）
    bool ShouldDropFrame() const;

    // 是否需要变速追赶（轻度/中度档）
    bool ShouldSpeedUp() const;

    // 当前追帧倍速（1.0 = 不追帧）
    double GetPlaybackRate() const;

    // 延迟误差（毫秒）= liveLatency - targetLatency
    double GetLatencyErrorMs() const;

    // 追帧已持续时长（毫秒；未追帧返回 0）
    double GetChaseDurationMs() const;

    // 当前追帧档位：0 正常 / 1 轻度 / 2 中度 / 3 重度 / 4 严重
    int GetChaseLevel() const;

    // 重置全部状态（切换媒体时调用）
    void Reset();

private:

    // 根据延迟误差计算目标档位（含滞回）
    int ComputeTargetLevel(
        double errorMs) const;

    // 档位对应的追帧倍速
    double RateOfLevel(
        int level) const;

    // 丢帧防抖判定（连续建议 + 冷却）
    bool ShouldDropDebounced() const;

    // ---------- 配置 ----------

    double targetLatencyMs = 300.0;      // 目标延迟（毫秒）

    double minLatencyMs = 100.0;         // 最低延迟（毫秒）

    double maxLatencyMs = 2000.0;        // 最高延迟（毫秒）

    // 档位阈值（毫秒）
    double thresholdLightMs = 200.0;

    double thresholdMediumMs = 350.0;

    double thresholdHeavyMs = 500.0;

    double thresholdAggressiveMs = 1000.0;

    // 档位倍速
    double speedLight = 1.03;

    double speedMedium = 1.08;

    double speedHeavy = 1.15;

    double speedAggressive = 1.25;

    double maxChaseStep = 0.02;          // 倍速平滑步长上限

    double hysteresisMs = 50.0;          // 降档滞回（毫秒）

    double dropCooldownMs = 150.0;       // 丢帧冷却（毫秒）

    int dropConsecutive = 3;             // 丢帧连续建议次数

    // ---------- 状态（mutex 保护） ----------

    mutable std::mutex mutex;

    int level = 0;                       // 当前档位

    double playbackRate = 1.0;           // 当前实际追帧倍速

    double latencyErrorMs = 0.0;         // 最近一次延迟误差

    // 丢帧防抖（const 查询方法内也会自增，需 mutable）
    mutable int dropHintCount = 0;       // 连续建议计数

    long long lastDropTickMs = 0;        // 上次丢帧时刻（steady ms）

    // 追帧计时
    long long chaseStartMs = -1;         // 进入追帧时刻（-1 = 未追帧）
};

#pragma once

// ============================================================
// AdaptiveBufferController - 自适应缓冲控制器（9.0）
//
// 背景（评审意见）：
//   旧版 NetworkBuffer 的 liveMaxQueueMs = 500 是静态策略：
//   队列积压超过固定阈值就丢旧包。弱网自适应要求缓冲时长
//   随网络状况动态调整。
//
// 目标缓冲（毫秒）随网络质量变化：
//
//   网络很好（jitter 小 + 无丢包） : 100ms（最低延迟）
//   轻微抖动                       : 200ms
//   严重抖动（jitter 大）          : 350ms
//   持续丢包                       : 500ms（保流畅优先）
//
// 变化平滑：每次 Update 目标最多移动 maxStepMs，避免
// 缓冲水位突变导致反复卡顿 / 反复丢包。
//
// 用法（渲染线程每帧或节流调用）：
//   adaptiveBuffer->Update(jitterMs, lossPercent, throughputKbps);
//   int target = adaptiveBuffer->GetTargetBufferMs();
//   networkBuffer->SetLiveDurationMs(target, ...);
//   if (adaptiveBuffer->ShouldDropOldData()) { ... }
//
// 线程安全：任意线程（内部互斥锁）。
// ============================================================

#include <mutex>

class AdaptiveBufferController
{
public:

    AdaptiveBufferController();

    // 重置（切换媒体时调用）
    void Reset();

    // 每帧/节流更新网络状况：
    //   jitterMs         : 网络抖动（毫秒）
    //   packetLoss       : 丢包率（百分比 0~100）
    //   throughputKbps   : 吞吐量（kbps，可选，0 = 未知）
    void Update(
        double jitterMs,
        double packetLoss,
        double throughputKbps);

    // 当前目标缓冲时长（毫秒）
    int GetTargetBufferMs() const;

    // 缓冲下限 / 上限（毫秒）
    int GetMinBufferMs() const;

    int GetMaxBufferMs() const;

    // 当前网络档位：0 良好 / 1 轻抖 / 2 重抖 / 3 持续丢包
    int GetNetworkLevel() const;

    // 是否应丢弃旧数据：
    // 网络已恢复（目标回到下限）但队列仍积压超过目标 + 余量，
    // 需要丢旧包把延迟压回目标
    bool ShouldDropOldData() const;

    // ---------- 配置（可选，默认值见下方） ----------

    void SetMinBufferMs(
        int ms);

    void SetMaxBufferMs(
        int ms);

    // 网络良好判定阈值：jitter 低于 smoothMs（默认 30）
    void SetJitterSmoothMs(
        double ms);

    // 严重抖动判定阈值：jitter 高于 heavyMs（默认 80）
    void SetJitterHeavyMs(
        double ms);

    // 持续丢包判定阈值（百分比，默认 1.0）
    void SetLossThresholdPercent(
        double percent);

    // 目标变化最大步长（毫秒 / 次 Update，默认 50）
    void SetMaxStepMs(
        int ms);

private:

    // 根据网络状况计算目标档位（0~3）
    int ComputeLevel(
        double jitterMs,
        double packetLoss) const;

    // 档位对应的目标缓冲（毫秒）
    int TargetOfLevel(
        int level) const;

    // ---------- 配置 ----------

    int minBufferMs = 100;               // 缓冲下限（毫秒）

    int maxBufferMs = 500;               // 缓冲上限（毫秒）

    double jitterSmoothMs = 30.0;        // 网络良好 jitter 阈值

    double jitterHeavyMs = 80.0;         // 严重抖动 jitter 阈值

    double lossThresholdPercent = 1.0;   // 持续丢包阈值（%）

    int maxStepMs = 50;                  // 目标变化步长上限（毫秒）

    // ---------- 状态（mutex 保护） ----------

    mutable std::mutex mutex;

    int level = 0;                       // 当前网络档位

    int targetBufferMs = 100;            // 当前目标缓冲（毫秒）

    double smoothedJitterMs = 0.0;       // 平滑后抖动（毫秒）

    double smoothedLoss = 0.0;           // 平滑后丢包率（%）

    // 持续丢包计数（连续多次超阈值才进入"持续丢包"档）
    int lossStreak = 0;

    static constexpr int LOSS_STREAK_LIMIT = 5;   // 连续丢包判定次数
};

#include "Network/AdaptiveBufferController.h"

#include <algorithm>
#include <cmath>

// ============================================================
// AdaptiveBufferController - 自适应缓冲控制器
// ============================================================

AdaptiveBufferController::AdaptiveBufferController()
{
}

void AdaptiveBufferController::Reset()
{
    std::lock_guard<std::mutex> lock(mutex);

    level = 0;

    targetBufferMs = minBufferMs;

    smoothedJitterMs = 0.0;

    smoothedLoss = 0.0;

    lossStreak = 0;
}

void AdaptiveBufferController::Update(
    double jitterMs,
    double packetLoss,
    double throughputKbps)
{
    (void)throughputKbps;

    std::lock_guard<std::mutex> lock(mutex);

    if (jitterMs < 0.0)
    {
        jitterMs = 0.0;
    }

    if (packetLoss < 0.0)
    {
        packetLoss = 0.0;
    }

    // 输入平滑（EWMA），避免单次尖峰直接跳档
    if (smoothedJitterMs <= 0.0)
    {
        smoothedJitterMs = jitterMs;

        smoothedLoss = packetLoss;
    }
    else
    {
        constexpr double ALPHA = 0.3;

        smoothedJitterMs +=
            ALPHA * (jitterMs - smoothedJitterMs);

        smoothedLoss +=
            ALPHA * (packetLoss - smoothedLoss);
    }

    // 持续丢包计数：连续 LOSS_STREAK_LIMIT 次超阈值才认定
    if (smoothedLoss >= lossThresholdPercent)
    {
        lossStreak =
            std::min(
                lossStreak + 1,
                LOSS_STREAK_LIMIT);
    }
    else
    {
        lossStreak = 0;
    }

    int newLevel =
        ComputeLevel(
            smoothedJitterMs,
            smoothedLoss);

    // 升档立即，降档只降一级（与 LiveLatencyController 一致）
    if (newLevel > level)
    {
        level = newLevel;
    }
    else if (newLevel < level)
    {
        level--;
    }

    // 目标缓冲平滑逼近：每次最多移动 maxStepMs
    int target =
        TargetOfLevel(level);

    int delta =
        target - targetBufferMs;

    int step =
        std::max(
            -maxStepMs,
            std::min(
                maxStepMs,
                delta));

    targetBufferMs += step;

    // 边界保护
    targetBufferMs =
        std::max(
            minBufferMs,
            std::min(
                maxBufferMs,
                targetBufferMs));
}

int AdaptiveBufferController::GetTargetBufferMs() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return targetBufferMs;
}

int AdaptiveBufferController::GetMinBufferMs() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return minBufferMs;
}

int AdaptiveBufferController::GetMaxBufferMs() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return maxBufferMs;
}

int AdaptiveBufferController::GetNetworkLevel() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return level;
}

bool AdaptiveBufferController::ShouldDropOldData() const
{
    std::lock_guard<std::mutex> lock(mutex);

    // 网络已恢复到良好档（目标回到下限附近），
    // 但队列仍积压超过 目标 + 余量（余量 = 1 档步长），
    // 需要丢旧包把延迟压回目标
    if (level != 0)
    {
        return false;
    }

    return targetBufferMs >
        minBufferMs + maxStepMs / 2;
}

// ============================================================
// 配置
// ============================================================

void AdaptiveBufferController::SetMinBufferMs(
    int ms)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (ms > 0 && ms <= maxBufferMs)
    {
        minBufferMs = ms;
    }
}

void AdaptiveBufferController::SetMaxBufferMs(
    int ms)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (ms >= minBufferMs)
    {
        maxBufferMs = ms;
    }
}

void AdaptiveBufferController::SetJitterSmoothMs(
    double ms)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (ms > 0.0 && ms < jitterHeavyMs)
    {
        jitterSmoothMs = ms;
    }
}

void AdaptiveBufferController::SetJitterHeavyMs(
    double ms)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (ms > jitterSmoothMs)
    {
        jitterHeavyMs = ms;
    }
}

void AdaptiveBufferController::SetLossThresholdPercent(
    double percent)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (percent > 0.0)
    {
        lossThresholdPercent = percent;
    }
}

void AdaptiveBufferController::SetMaxStepMs(
    int ms)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (ms > 0)
    {
        maxStepMs = ms;
    }
}

// ============================================================
// 内部
// ============================================================

int AdaptiveBufferController::ComputeLevel(
    double jitterMs,
    double packetLoss) const
{
    // 持续丢包：最高优先级（流畅优先）
    if (lossStreak >= LOSS_STREAK_LIMIT)
    {
        return 3;
    }

    // 严重抖动
    if (jitterMs >= jitterHeavyMs)
    {
        return 2;
    }

    // 轻微抖动（介于良好与严重之间）
    if (jitterMs >= jitterSmoothMs)
    {
        return 1;
    }

    // 网络良好
    return 0;
}

int AdaptiveBufferController::TargetOfLevel(
    int level) const
{
    switch (level)
    {
    case 3:
        // 持续丢包：保流畅，缓冲拉满
        return maxBufferMs;

    case 2:
        // 严重抖动：中高缓冲
        return minBufferMs + (maxBufferMs - minBufferMs) * 5 / 8;

    case 1:
        // 轻微抖动：中低缓冲
        return minBufferMs + (maxBufferMs - minBufferMs) * 1 / 4;

    default:
        // 网络良好：最低延迟
        return minBufferMs;
    }
}

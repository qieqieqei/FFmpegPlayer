#include "Sync/LiveLatencyController.h"

#include <algorithm>

// ============================================================
// LiveLatencyController - 直播延迟追帧控制器（9.0）
// ============================================================

namespace
{
    // 当前墙钟（毫秒，steady_clock）
    long long NowMs()
    {
        return std::chrono::duration_cast<
            std::chrono::milliseconds>(
                std::chrono::steady_clock::now()
                    .time_since_epoch())
            .count();
    }
}

LiveLatencyController::LiveLatencyController()
{
}

// ============================================================
// 目标 / 边界
// ============================================================

void LiveLatencyController::SetTargetLatencyMs(
    double latencyMs)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (latencyMs > 0.0)
    {
        targetLatencyMs = latencyMs;
    }
}

double LiveLatencyController::GetTargetLatencyMs() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return targetLatencyMs;
}

void LiveLatencyController::SetMinLatencyMs(
    double latencyMs)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (latencyMs >= 0.0)
    {
        minLatencyMs = latencyMs;
    }
}

void LiveLatencyController::SetMaxLatencyMs(
    double latencyMs)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (latencyMs > minLatencyMs)
    {
        maxLatencyMs = latencyMs;
    }
}

// ============================================================
// 追帧档位配置
// ============================================================

void LiveLatencyController::SetChaseSpeedLight(
    double speed)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (speed >= 1.0)
    {
        speedLight = speed;
    }
}

void LiveLatencyController::SetChaseSpeedMedium(
    double speed)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (speed >= 1.0)
    {
        speedMedium = speed;
    }
}

void LiveLatencyController::SetChaseSpeedHeavy(
    double speed)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (speed >= 1.0)
    {
        speedHeavy = speed;
    }
}

void LiveLatencyController::SetChaseSpeedAggressive(
    double speed)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (speed >= 1.0)
    {
        speedAggressive = speed;
    }
}

void LiveLatencyController::SetChaseThresholdLightMs(
    double ms)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (ms > 0.0)
    {
        thresholdLightMs = ms;
    }
}

void LiveLatencyController::SetChaseThresholdMediumMs(
    double ms)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (ms > thresholdLightMs)
    {
        thresholdMediumMs = ms;
    }
}

void LiveLatencyController::SetChaseThresholdHeavyMs(
    double ms)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (ms > thresholdMediumMs)
    {
        thresholdHeavyMs = ms;
    }
}

void LiveLatencyController::SetChaseThresholdAggressiveMs(
    double ms)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (ms > thresholdHeavyMs)
    {
        thresholdAggressiveMs = ms;
    }
}

// ============================================================
// 平滑 / 防抖
// ============================================================

void LiveLatencyController::SetMaxChaseStep(
    double step)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (step > 0.0)
    {
        maxChaseStep = step;
    }
}

void LiveLatencyController::SetHysteresisMs(
    double ms)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (ms >= 0.0)
    {
        hysteresisMs = ms;
    }
}

void LiveLatencyController::SetDropCooldownMs(
    double ms)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (ms >= 0.0)
    {
        dropCooldownMs = ms;
    }
}

void LiveLatencyController::SetDropConsecutive(
    int count)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (count >= 1)
    {
        dropConsecutive = count;
    }
}

// ============================================================
// 每帧更新
// ============================================================

void LiveLatencyController::Update(
    double liveLatencyMs,
    double bufferMs,
    double jitterMs,
    double audioVideoDiffMs)
{
    std::lock_guard<std::mutex> lock(mutex);

    // 边界保护：无效输入忽略（不破坏状态）
    if (liveLatencyMs < 0.0)
    {
        liveLatencyMs = 0.0;
    }

    // 延迟误差 = 当前延迟 - 目标延迟（正 = 积压，需要追赶）
    latencyErrorMs =
        liveLatencyMs - targetLatencyMs;

    // 目标档位（含滞回：升档立即，降档需回落超过滞回量）
    level =
        ComputeTargetLevel(latencyErrorMs);

    // 目标倍速（按档位）
    double targetRate =
        RateOfLevel(level);

    // 平滑逼近：每次最多移动 maxChaseStep
    double delta =
        targetRate - playbackRate;

    double step =
        std::max(
            -maxChaseStep,
            std::min(
                maxChaseStep,
                delta));

    playbackRate += step;

    // 倍速回到 1.0（误差极小）时完全复位
    if (playbackRate < 1.0 + 1e-6)
    {
        playbackRate = 1.0;
    }

    // 追帧计时
    long long now = NowMs();

    if (playbackRate > 1.0 + 1e-6)
    {
        if (chaseStartMs < 0)
        {
            chaseStartMs = now;
        }
    }
    else
    {
        chaseStartMs = -1;
    }

    // 丢帧防抖状态（连续建议计数随档位复位）
    if (level < 3)
    {
        dropHintCount = 0;
    }
}

// ============================================================
// 决策输出
// ============================================================

bool LiveLatencyController::ShouldChase() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return playbackRate > 1.0 + 1e-6;
}

bool LiveLatencyController::ShouldDropFrame() const
{
    std::lock_guard<std::mutex> lock(mutex);

    // 仅重度（3）/ 严重（4）档丢帧
    if (level < 3)
    {
        return false;
    }

    // 冷却中：不丢（给变速追赶时间）
    if (dropCooldownMs > 0.0)
    {
        long long now = NowMs();

        if (now - lastDropTickMs <
            static_cast<long long>(dropCooldownMs))
        {
            return false;
        }
    }

    // 连续建议：连续 N 次 Update 都建议丢帧才真正丢，
    // 避免单帧延迟尖峰误丢
    dropHintCount++;

    return dropHintCount >= dropConsecutive;
}

bool LiveLatencyController::ShouldSpeedUp() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return level >= 1 && level <= 2;
}

double LiveLatencyController::GetPlaybackRate() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return playbackRate;
}

double LiveLatencyController::GetLatencyErrorMs() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return latencyErrorMs;
}

double LiveLatencyController::GetChaseDurationMs() const
{
    std::lock_guard<std::mutex> lock(mutex);

    if (chaseStartMs < 0)
    {
        return 0.0;
    }

    return static_cast<double>(
        NowMs() - chaseStartMs);
}

int LiveLatencyController::GetChaseLevel() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return level;
}

void LiveLatencyController::Reset()
{
    std::lock_guard<std::mutex> lock(mutex);

    level = 0;

    playbackRate = 1.0;

    latencyErrorMs = 0.0;

    dropHintCount = 0;

    lastDropTickMs = 0;

    chaseStartMs = -1;
}

// ============================================================
// 内部
// ============================================================

int LiveLatencyController::ComputeTargetLevel(
    double errorMs) const
{
    // 升档判定：原始阈值（积压加剧，立即升档）
    int up = 0;

    if (errorMs >= thresholdAggressiveMs)
    {
        up = 4;
    }
    else if (errorMs >= thresholdHeavyMs)
    {
        up = 3;
    }
    else if (errorMs >= thresholdMediumMs)
    {
        up = 2;
    }
    else if (errorMs >= thresholdLightMs)
    {
        up = 1;
    }

    if (up > level)
    {
        return up;
    }

    // 降档判定：需回落到 当前档位阈值 - 滞回 以下（防抖振）
    if (level >= 4 &&
        errorMs < thresholdAggressiveMs - hysteresisMs)
    {
        return 3;
    }

    if (level >= 3 &&
        errorMs < thresholdHeavyMs - hysteresisMs)
    {
        return 2;
    }

    if (level >= 2 &&
        errorMs < thresholdMediumMs - hysteresisMs)
    {
        return 1;
    }

    if (level >= 1 &&
        errorMs < thresholdLightMs - hysteresisMs)
    {
        return 0;
    }

    // 保持当前档位
    return level;
}

double LiveLatencyController::RateOfLevel(
    int level) const
{
    switch (level)
    {
    case 4:
        return speedAggressive;

    case 3:
        return speedHeavy;

    case 2:
        return speedMedium;

    case 1:
        return speedLight;

    default:
        return 1.0;
    }
}

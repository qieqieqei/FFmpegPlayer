#include "Network/LatencyEstimator.h"

#include <algorithm>
#include <cmath>

// ============================================================
// LatencyEstimator - 直播延迟估算器
// ============================================================

LatencyEstimator::LatencyEstimator()
{
}

double LatencyEstimator::NowMs()
{
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now()
            .time_since_epoch())
        .count() * 1000.0;
}

void LatencyEstimator::Reset()
{
    std::lock_guard<std::mutex> lock(mutex);

    haveLastArrival = false;

    avgIntervalMs = 0.0;

    jitterMs = 0.0;

    decodeLatencyMs = 0.0;

    renderLatencyMs = 0.0;

    bufferLatencyMs = 0.0;

    haveDecodeTime = false;
}

// ============================================================
// 测量点
// ============================================================

void LatencyEstimator::OnPacket(
    int64_t pts,
    std::chrono::steady_clock::time_point arrival)
{
    (void)pts;

    std::lock_guard<std::mutex> lock(mutex);

    if (!haveLastArrival)
    {
        // 第一个包：只建立基准
        haveLastArrival = true;

        lastArrival = arrival;

        return;
    }

    // 到达间隔（毫秒）
    double intervalMs =
        std::chrono::duration<double>(
            arrival - lastArrival)
            .count() * 1000.0;

    lastArrival = arrival;

    if (intervalMs < 0.0)
    {
        intervalMs = 0.0;
    }

    // 平均间隔 EWMA
    if (avgIntervalMs <= 0.0)
    {
        avgIntervalMs = intervalMs;
    }
    else
    {
        avgIntervalMs +=
            JITTER_ALPHA *
            (intervalMs - avgIntervalMs);
    }

    // 抖动 EWMA（RFC3550 风格：|间隔 - 平均| 的平滑）
    double diffMs =
        std::fabs(intervalMs - avgIntervalMs);

    if (jitterMs <= 0.0)
    {
        jitterMs = diffMs;
    }
    else
    {
        jitterMs +=
            JITTER_ALPHA *
            (diffMs - jitterMs);
    }
}

void LatencyEstimator::OnFrameDecoded(
    double pts)
{
    (void)pts;

    std::lock_guard<std::mutex> lock(mutex);

    auto now =
        std::chrono::steady_clock::now();

    // 解码队列延迟 ≈ 包到达 -> 帧解码 的墙钟差
    // （无逐包关联时的近似：最近包到达到解码完成的间隔）
    if (haveLastArrival)
    {
        double queueMs =
            std::chrono::duration<double>(
                now - lastArrival)
                .count() * 1000.0;

        if (queueMs >= 0.0)
        {
            if (decodeLatencyMs <= 0.0)
            {
                decodeLatencyMs = queueMs;
            }
            else
            {
                decodeLatencyMs +=
                    JITTER_ALPHA *
                    (queueMs - decodeLatencyMs);
            }
        }
    }

    lastDecodeTime = now;

    haveDecodeTime = true;
}

void LatencyEstimator::OnFrameRendered(
    double pts)
{
    (void)pts;

    std::lock_guard<std::mutex> lock(mutex);

    if (!haveDecodeTime)
    {
        return;
    }

    auto now =
        std::chrono::steady_clock::now();

    double renderMs =
        std::chrono::duration<double>(
            now - lastDecodeTime)
            .count() * 1000.0;

    if (renderMs < 0.0)
    {
        return;
    }

    if (renderLatencyMs <= 0.0)
    {
        renderLatencyMs = renderMs;
    }
    else
    {
        renderLatencyMs +=
            JITTER_ALPHA *
            (renderMs - renderLatencyMs);
    }
}

void LatencyEstimator::SetBufferLatencyMs(
    double ms)
{
    std::lock_guard<std::mutex> lock(mutex);

    bufferLatencyMs =
        ms < 0.0 ? 0.0 : ms;
}

// ============================================================
// 读取
// ============================================================

double LatencyEstimator::GetNetworkLatencyMs() const
{
    std::lock_guard<std::mutex> lock(mutex);

    // 无发送端时钟：用到达抖动保守推导（3 × jitter）
    return jitterMs * NETWORK_LATENCY_FACTOR;
}

double LatencyEstimator::GetPlaybackLatencyMs() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return
        bufferLatencyMs +
        decodeLatencyMs +
        renderLatencyMs;
}

double LatencyEstimator::GetEndToEndLatencyMs() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return
        jitterMs * NETWORK_LATENCY_FACTOR +
        bufferLatencyMs +
        decodeLatencyMs +
        renderLatencyMs;
}

double LatencyEstimator::GetJitterMs() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return jitterMs;
}

double LatencyEstimator::GetDecodeLatencyMs() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return decodeLatencyMs;
}

double LatencyEstimator::GetRenderLatencyMs() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return renderLatencyMs;
}

double LatencyEstimator::GetBufferLatencyMs() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return bufferLatencyMs;
}

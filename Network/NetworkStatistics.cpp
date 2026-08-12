#include "Network/NetworkStatistics.h"

#include <sstream>
#include <iomanip>
#include <cmath>

// ============================================================
// NetworkStatistics - 网络流统计（7.2 / 9.0 增强）
// ============================================================

NetworkStatistics::NetworkStatistics()
{
    lastTick =
        std::chrono::steady_clock::now();
}

void NetworkStatistics::Reset()
{
    std::lock_guard<std::mutex> lock(mutex);

    windowPackets = 0;

    windowBytes = 0;

    windowDropped = 0;

    windowDecoded = 0;

    windowRendered = 0;

    inputFps = 0.0;

    outputFps = 0.0;

    bitrateKbps = 0.0;

    lossPercent = 0.0;

    bufferLevel.store(0);

    bufferMax.store(0);

    latencyMs.store(0);

    buffering.store(false);

    // 9.0 状态
    haveLastArrival = false;

    avgIntervalMs = 0.0;

    jitterMs = 0.0;

    jitterVariance = 0.0;

    prevJitterMs = 0.0;

    decodeLatencyMs = 0.0;

    renderLatencyMs = 0.0;

    bufferLatencyMs = 0.0;

    stallCount = 0;

    stallDurationMs = 0.0;

    inStall = false;

    windowDroppedFrames = 0;

    droppedFrameCount = 0;

    renderedTotal = 0;

    dropFrameRate = 0.0;

    lastTick =
        std::chrono::steady_clock::now();
}

// ============================================================
// 输入侧
// ============================================================

void NetworkStatistics::OnPacketReceived(
    int bytes)
{
    std::lock_guard<std::mutex> lock(mutex);

    windowPackets++;

    windowBytes += bytes;

    // 9.0：到达间隔 -> 抖动（EWMA）
    UpdateJitterLocked();

    Tick();
}

void NetworkStatistics::OnPacketDropped(
    int64_t count)
{
    if (count <= 0)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex);

    // 窗口计数为 int，钳制避免极端值溢出
    windowDropped +=
        static_cast<int>(
            std::min<int64_t>(count, 1000000));

    Tick();
}

void NetworkStatistics::OnFrameDecoded()
{
    std::lock_guard<std::mutex> lock(mutex);

    windowDecoded++;

    Tick();
}

// ============================================================
// 输出侧
// ============================================================

void NetworkStatistics::OnFrameRendered()
{
    std::lock_guard<std::mutex> lock(mutex);

    windowRendered++;

    renderedTotal++;

    Tick();
}

void NetworkStatistics::OnVideoFrameDropped()
{
    std::lock_guard<std::mutex> lock(mutex);

    windowDroppedFrames++;

    droppedFrameCount++;

    Tick();
}

// ============================================================
// 卡顿
// ============================================================

void NetworkStatistics::OnStallStart()
{
    std::lock_guard<std::mutex> lock(mutex);

    if (inStall)
    {
        return;
    }

    inStall = true;

    stallStart =
        std::chrono::steady_clock::now();
}

void NetworkStatistics::OnStallEnd()
{
    std::lock_guard<std::mutex> lock(mutex);

    if (!inStall)
    {
        return;
    }

    inStall = false;

    stallCount++;

    double ms =
        std::chrono::duration<double>(
            std::chrono::steady_clock::now() -
            stallStart)
            .count() * 1000.0;

    stallDurationMs +=
        ms < 0.0 ? 0.0 : ms;
}

// ============================================================
// 缓冲 / 延迟
// ============================================================

void NetworkStatistics::SetBufferLevel(
    int packets,
    int maxPackets)
{
    bufferLevel.store(packets);

    bufferMax.store(maxPackets);

    // 缓冲为空：视为饥饿（等待网络数据）
    buffering.store(packets <= 0);
}

void NetworkStatistics::SetLatencyMs(
    int ms)
{
    latencyMs.store(ms < 0 ? 0 : ms);
}

void NetworkStatistics::SetDecodeLatencyMs(
    double ms)
{
    std::lock_guard<std::mutex> lock(mutex);

    decodeLatencyMs =
        ms < 0.0 ? 0.0 : ms;
}

void NetworkStatistics::SetRenderLatencyMs(
    double ms)
{
    std::lock_guard<std::mutex> lock(mutex);

    renderLatencyMs =
        ms < 0.0 ? 0.0 : ms;
}

void NetworkStatistics::SetBufferLatencyMs(
    double ms)
{
    std::lock_guard<std::mutex> lock(mutex);

    bufferLatencyMs =
        ms < 0.0 ? 0.0 : ms;
}

// ============================================================
// 读取
// ============================================================

double NetworkStatistics::GetInputFps() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return inputFps;
}

double NetworkStatistics::GetOutputFps() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return outputFps;
}

double NetworkStatistics::GetBitrateKbps() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return bitrateKbps;
}

double NetworkStatistics::GetPacketLossPercent() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return lossPercent;
}

int NetworkStatistics::GetBufferLevel() const
{
    return bufferLevel.load();
}

int NetworkStatistics::GetBufferMax() const
{
    return bufferMax.load();
}

int NetworkStatistics::GetLatencyMs() const
{
    return latencyMs.load();
}

bool NetworkStatistics::IsBuffering() const
{
    return buffering.load();
}

// ============================================================
// 9.0 增强读取
// ============================================================

double NetworkStatistics::GetJitterMs() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return jitterMs;
}

double NetworkStatistics::GetThroughputKbps() const
{
    std::lock_guard<std::mutex> lock(mutex);

    // 与码率同源（每秒字节数换算）
    return bitrateKbps;
}

double NetworkStatistics::GetDecodeLatencyMs() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return decodeLatencyMs;
}

double NetworkStatistics::GetRenderLatencyMs() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return renderLatencyMs;
}

double NetworkStatistics::GetBufferLatencyMs() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return bufferLatencyMs;
}

double NetworkStatistics::GetEndToEndLatencyMs() const
{
    std::lock_guard<std::mutex> lock(mutex);

    // 显式设置过：优先返回显式值（兼容旧行为）
    int explicitMs = latencyMs.load();

    if (explicitMs > 0)
    {
        return static_cast<double>(explicitMs);
    }

    // 内部组装：网络估算（3×jitter）+ 各分量
    return
        jitterMs * NETWORK_LATENCY_FACTOR +
        decodeLatencyMs +
        renderLatencyMs +
        bufferLatencyMs;
}

double NetworkStatistics::GetLatencyVariance() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return jitterVariance;
}

int NetworkStatistics::GetStallCount() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return stallCount;
}

double NetworkStatistics::GetStallDurationMs() const
{
    std::lock_guard<std::mutex> lock(mutex);

    // 正在卡顿：加上进行中的时长
    double total = stallDurationMs;

    if (inStall)
    {
        double ms =
            std::chrono::duration<double>(
                std::chrono::steady_clock::now() -
                stallStart)
                .count() * 1000.0;

        total +=
            ms < 0.0 ? 0.0 : ms;
    }

    return total;
}

int NetworkStatistics::GetDroppedFrameCount() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return droppedFrameCount;
}

double NetworkStatistics::GetDropFrameRate() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return dropFrameRate;
}

std::string NetworkStatistics::ToString() const
{
    std::lock_guard<std::mutex> lock(mutex);

    std::ostringstream oss;

    oss
        << "Net | In "
        << std::fixed
        << std::setprecision(1)
        << inputFps
        << "fps | Out "
        << outputFps
        << "fps | "
        << static_cast<int>(bitrateKbps)
        << "kbps | Loss "
        << std::setprecision(1)
        << lossPercent
        << "% | Buf "
        << bufferLevel.load()
        << "/"
        << bufferMax.load()
        << " | "
        << latencyMs.load()
        << "ms";

    // 9.0：抖动 / 丢帧
    oss
        << " | Jit "
        << std::setprecision(1)
        << jitterMs
        << "ms";

    if (droppedFrameCount > 0)
    {
        oss
            << " | Drop "
            << droppedFrameCount
            << " ("
            << std::setprecision(1)
            << dropFrameRate
            << "%)";
    }

    if (stallCount > 0)
    {
        oss
            << " | Stall "
            << stallCount
            << "x "
            << static_cast<int>(stallDurationMs)
            << "ms";
    }

    return oss.str();
}

// ============================================================
// 每秒结算一次滑动窗口
// ============================================================

void NetworkStatistics::Tick()
{
    auto now =
        std::chrono::steady_clock::now();

    double elapsed =
        std::chrono::duration<double>(
            now - lastTick).count();

    // 不足 1 秒：继续累积
    if (elapsed < 1.0)
    {
        return;
    }

    // 结算：速率 = 窗口计数 / 实际耗时
    inputFps =
        windowDecoded / elapsed;

    outputFps =
        windowRendered / elapsed;

    bitrateKbps =
        (windowBytes * 8.0 / 1000.0) / elapsed;

    int total =
        windowPackets + windowDropped;

    lossPercent =
        total > 0 ?
        (windowDropped * 100.0) / total :
        0.0;

    // 渲染丢帧率 = 丢帧数 /（渲染 + 丢帧）
    int renderTotal =
        windowRendered + windowDroppedFrames;

    dropFrameRate =
        renderTotal > 0 ?
        (windowDroppedFrames * 100.0) / renderTotal :
        0.0;

    // 清零窗口
    windowPackets = 0;

    windowBytes = 0;

    windowDropped = 0;

    windowDecoded = 0;

    windowRendered = 0;

    windowDroppedFrames = 0;

    lastTick = now;
}

void NetworkStatistics::UpdateJitterLocked()
{
    auto now =
        std::chrono::steady_clock::now();

    if (!haveLastArrival)
    {
        // 第一个包：只建立基准
        haveLastArrival = true;

        lastArrival = now;

        return;
    }

    // 到达间隔（毫秒）
    double intervalMs =
        std::chrono::duration<double>(
            now - lastArrival)
            .count() * 1000.0;

    lastArrival = now;

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

    // 抖动 EWMA（RFC3550 风格）
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

    // 延迟方差：抖动变化量的 EWMA
    double jitterDelta =
        std::fabs(jitterMs - prevJitterMs);

    if (prevJitterMs > 0.0)
    {
        if (jitterVariance <= 0.0)
        {
            jitterVariance =
                jitterDelta * jitterDelta;
        }
        else
        {
            jitterVariance +=
                JITTER_ALPHA *
                (jitterDelta * jitterDelta -
                 jitterVariance);
        }
    }

    prevJitterMs = jitterMs;
}

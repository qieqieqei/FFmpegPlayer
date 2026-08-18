#include "Network/NetworkStatistics.h"

#include <sstream>
#include <iomanip>

// ============================================================
// NetworkStatistics - 网络流统计
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

    totalDropped.store(0);

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

    windowDropped += count;

    totalDropped.fetch_add(count);

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

    Tick();
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

int64_t NetworkStatistics::GetDroppedPackets() const
{
    return totalDropped.load();
}

bool NetworkStatistics::IsBuffering() const
{
    return buffering.load();
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

    // 清零窗口
    windowPackets = 0;

    windowBytes = 0;

    windowDropped = 0;

    windowDecoded = 0;

    windowRendered = 0;

    lastTick = now;
}

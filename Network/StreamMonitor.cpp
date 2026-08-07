#include "Network/StreamMonitor.h"

#include "Utils/Logger.h"

#include <cstdio>

// 内部辅助：百分数（1 位小数）
static std::string formatPercent(double v);

// 内部辅助：数字去尾零
static std::string trimZero(double v);

// ============================================================
// StreamMonitor - 流媒体监控
// ============================================================

StreamMonitor::StreamMonitor()
{
    lastTick =
        std::chrono::steady_clock::now();

    lastDataTime =
        std::chrono::steady_clock::now();
}

void StreamMonitor::Init(
    NetworkStatistics* stats,
    const StreamConfig& cfg)
{
    this->stats = stats;

    this->cfg = cfg;

    Reset();
}

void StreamMonitor::Tick()
{
    auto now =
        std::chrono::steady_clock::now();

    // 节流：未到间隔则跳过
    if (!firstTick)
    {
        auto elapsedMs =
            std::chrono::duration_cast<
                std::chrono::milliseconds>(
                    now - lastTick)
                .count();

        if (elapsedMs < intervalMs)
        {
            return;
        }
    }

    firstTick = false;

    lastTick = now;

    Evaluate();
}

void StreamMonitor::LogSummary()
{
    Evaluate();
}

void StreamMonitor::Reset()
{
    lastAlert.clear();

    stalled = false;

    firstTick = true;

    lastDataTime =
        std::chrono::steady_clock::now();
}

void StreamMonitor::SetLatencyWarnMs(int ms)
{
    if (ms > 0)
    {
        latencyWarnMs = ms;
    }
}

void StreamMonitor::SetLossWarnPercent(double p)
{
    if (p > 0.0)
    {
        lossWarnPercent = p;
    }
}

void StreamMonitor::SetStallTimeoutSec(int sec)
{
    if (sec > 0)
    {
        stallTimeoutSec = sec;
    }
}

void StreamMonitor::SetIntervalMs(int ms)
{
    if (ms > 0)
    {
        intervalMs = ms;
    }
}

std::string StreamMonitor::GetLastAlert() const
{
    return lastAlert;
}

bool StreamMonitor::IsStalled() const
{
    return stalled;
}

// ============================================================
// 内部
// ============================================================

void StreamMonitor::Evaluate()
{
    if (!stats)
    {
        return;
    }

    const auto now =
        std::chrono::steady_clock::now();

    // ---------- 数据活性 ----------

    // 用码率 + 输入 FPS 判断是否有数据到达
    bool hasData =
        stats->GetBitrateKbps() > 0.0 ||
        stats->GetInputFps() > 0.0;

    if (hasData)
    {
        lastDataTime = now;
    }

    double idleSec =
        std::chrono::duration<double>(
            now - lastDataTime)
            .count();

    bool nowStalled =
        idleSec >= stallTimeoutSec;

    if (nowStalled && !stalled)
    {
        Alert(
            "ERROR",
            "Stream stalled : no data for " +
            std::to_string(
                static_cast<int>(idleSec)) +
            "s");
    }
    else if (!nowStalled && stalled)
    {
        // 恢复
        Logger::Info()
            << "[StreamMonitor] Stream recovered"
            << std::endl;

        lastAlert.clear();
    }

    stalled = nowStalled;

    // ---------- 统计摘要 ----------

    double loss =
        stats->GetPacketLossPercent();

    int latency =
        stats->GetLatencyMs();

    std::string line =
        "RTSP Delay: " +
        std::to_string(latency) +
        "ms, Packet Loss: " +
        formatPercent(loss) +
        "%, Bitrate " +
        std::to_string(
            static_cast<int>(
                stats->GetBitrateKbps())) +
        "kbps, Buf " +
        std::to_string(
            stats->GetBufferLevel()) +
        "/" +
        std::to_string(
            stats->GetBufferMax()) +
        ", FPS " +
        trimZero(
            stats->GetInputFps()) +
        "/" +
        trimZero(
            stats->GetOutputFps());

    Logger::Info()
        << "[StreamMonitor] "
        << line
        << std::endl;

    // ---------- 告警 ----------

    // 延迟
    if (latency >= latencyWarnMs)
    {
        Alert(
            "WARN",
            "Latency high : " +
            std::to_string(latency) +
            "ms (>= " +
            std::to_string(latencyWarnMs) +
            "ms)");
    }

    // 丢包率
    if (loss >= lossWarnPercent)
    {
        Alert(
            "WARN",
            "Packet loss high : " +
            formatPercent(loss) +
            "% (>= " +
            trimZero(lossWarnPercent) +
            "%)");
    }

    // 缓冲饥饿
    if (stats->IsBuffering())
    {
        Alert(
            "WARN",
            "Buffer underrun (buffering)");
    }
}

void StreamMonitor::Alert(
    const std::string& level,
    const std::string& text)
{
    // 去重：同一告警不重复刷屏
    if (text == lastAlert)
    {
        return;
    }

    lastAlert = text;

    if (level == "ERROR")
    {
        Logger::Error()
            << "[StreamMonitor] ["
            << level
            << "] "
            << text
            << std::endl;
    }
    else
    {
        Logger::Warn()
            << "[StreamMonitor] ["
            << level
            << "] "
            << text
            << std::endl;
    }
}

// 百分数格式化：保留 1 位小数
static std::string formatPercent(
    double v)
{
    char buf[32] = { 0 };

    snprintf(buf, sizeof(buf), "%.1f", v);

    return std::string(buf);
}

// 数字格式化：去掉多余小数
static std::string trimZero(
    double v)
{
    char buf[32] = { 0 };

    snprintf(buf, sizeof(buf), "%.2f", v);

    std::string s(buf);

    // 去掉末尾 0（保留 1 位）
    while (s.size() > 1 &&
           s.back() == '0')
    {
        s.pop_back();
    }

    if (!s.empty() && s.back() == '.')
    {
        s.pop_back();
    }

    return s;
}

#pragma once

// ============================================================
// StreamMonitor - 流媒体监控（7.9）
//
// 周期性巡检网络流健康度，输出监控摘要 + 阈值告警：
//
//   正常输出（每秒一条）：
//     [StreamMonitor] RTSP Delay: 120ms, Packet Loss: 0.2%
//                     Bitrate 4200kbps, Buf 45/600, FPS 30.0/29.8
//
//   告警（超过阈值时输出，去重不刷屏）：
//     [StreamMonitor] [WARN] Latency high : 812ms (>= 500ms)
//     [StreamMonitor] [WARN] Packet loss high : 3.5%
//     [StreamMonitor] [ERROR] Stream stalled : no data 5s
//     [StreamMonitor] [WARN] Buffer underrun (buffering)
//
// 数据来源：NetworkStatistics（引用，不拷贝）。
// 线程归属：渲染线程或独立监控线程调用 Tick()。
// ============================================================

#include <chrono>
#include <string>

#include "Network/NetworkStatistics.h"
#include "Config/StreamConfig.h"

class StreamMonitor
{
public:

    StreamMonitor();

    // 绑定统计源与配置（阈值）
    void Init(
        NetworkStatistics* stats,
        const StreamConfig& cfg);

    // 巡检一次（内部按 intervalMs 节流；到点才真正统计）
    void Tick();

    // 强制立即输出一行摘要（不节流）
    void LogSummary();

    // 重置状态（切换媒体 / 重连时调用）
    void Reset();

    // ---------- 阈值配置（Init 后仍可改） ----------

    void SetLatencyWarnMs(int ms);      // 延迟告警阈值，默认 500

    void SetLossWarnPercent(double p);  // 丢包率告警阈值，默认 1.0

    void SetStallTimeoutSec(int sec);   // 无数据判定超时，默认 5

    void SetIntervalMs(int ms);         // 巡检间隔，默认 1000

    // ---------- 状态查询 ----------

    // 最近一次告警文本（无告警返回空串）
    std::string GetLastAlert() const;

    // 是否认为流已中断（stalled）
    bool IsStalled() const;

private:

    // 结算一次：读统计 + 判定告警
    void Evaluate();

    // 输出告警（去重）
    void Alert(
        const std::string& level,
        const std::string& text);

    NetworkStatistics* stats = nullptr;   // 统计源（借用）

    StreamConfig cfg;                     // 配置

    // 阈值
    int latencyWarnMs = 500;

    double lossWarnPercent = 1.0;

    int stallTimeoutSec = 5;

    int intervalMs = 1000;

    // 节流
    std::chrono::steady_clock::time_point lastTick;

    bool firstTick = true;

    // 去重：上次告警文本
    std::string lastAlert;

    // 状态
    bool stalled = false;

    std::chrono::steady_clock::time_point lastDataTime;

    int summaryCount = 0;      // 摘要输出计数（调试辅助）
};

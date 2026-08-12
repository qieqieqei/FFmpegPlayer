#pragma once

// ============================================================
// LatencyEstimator - 直播延迟估算器（9.0）
//
// 背景（评审意见）：
//   旧版 NetworkStatistics::SetLatencyMs() 只是外部写入一个
//   延迟数值，并没有真正建立
//     Camera capture -> Encoder -> Network -> Decoder -> Render
//   的时间戳链路，不能严格称为"端到端延迟"。
//
// 限制（诚实声明）：
//   没有发送端 timestamp / RTCP / 同步时钟时，真正的端到端
//   latency 无法精确计算。本类第一阶段做的是：
//
//     estimated live latency =
//       网络延迟估算（由到达抖动推导）
//     + 缓冲延迟（队列积压时长，外部写入）
//     + 解码队列延迟（包到达 -> 帧解码）
//     + 渲染延迟（帧解码 -> 帧渲染）
//
// 测量点：
//   OnPacket       Demux 线程（包到达时刻 / 到达间隔 -> jitter）
//   OnFrameDecoded Video 线程（解码出帧时刻）
//   OnFrameRendered Render 线程（渲染时刻）
//   SetBufferLatencyMs 任意线程（外部写入队列时长）
//
// 线程安全：内部互斥锁保护。
// ============================================================

#include <cstdint>
#include <mutex>
#include <chrono>

class LatencyEstimator
{
public:

    LatencyEstimator();

    // 重置全部状态（切换媒体 / 重连时调用）
    void Reset();

    // ---------- 测量点 ----------

    // 收到一个网络包（Demux 线程）
    // pts     : 包时间戳（媒体时间轴，秒；缺省传 AV_NOPTS_VALUE）
    // arrival : 到达墙钟时刻
    void OnPacket(
        int64_t pts,
        std::chrono::steady_clock::time_point arrival);

    // 解码出一帧（Video 线程）
    void OnFrameDecoded(
        double pts);

    // 渲染了一帧（Render 线程）
    void OnFrameRendered(
        double pts);

    // 缓冲时长（毫秒，外部写入：NetworkBuffer 队列时长估算）
    void SetBufferLatencyMs(
        double ms);

    // ---------- 读取 ----------

    // 网络延迟估算（毫秒）。
    // 无发送端时钟，用到达抖动推导：3 × jitter（保守估算）。
    double GetNetworkLatencyMs() const;

    // 播放侧延迟（毫秒）= buffer + decode + render
    double GetPlaybackLatencyMs() const;

    // 估算端到端延迟（毫秒）= network + playback
    double GetEndToEndLatencyMs() const;

    // 网络抖动（毫秒，RFC3550 风格 EWMA）
    double GetJitterMs() const;

    // 解码队列延迟（毫秒，EWMA）
    double GetDecodeLatencyMs() const;

    // 渲染延迟（毫秒，EWMA）
    double GetRenderLatencyMs() const;

    // 缓冲延迟（毫秒）
    double GetBufferLatencyMs() const;

private:

    // 当前墙钟（毫秒）
    static double NowMs();

    // ---------- 状态（mutex 保护） ----------

    mutable std::mutex mutex;

    // 到达间隔统计
    bool haveLastArrival = false;          // 是否已有上次到达时刻

    std::chrono::steady_clock::time_point lastArrival;

    double avgIntervalMs = 0.0;            // 平均到达间隔（毫秒）

    double jitterMs = 0.0;                 // 抖动（毫秒，EWMA）

    // 延迟分量（毫秒，EWMA）
    double decodeLatencyMs = 0.0;          // 包到达 -> 帧解码

    double renderLatencyMs = 0.0;          // 帧解码 -> 帧渲染

    double bufferLatencyMs = 0.0;          // 缓冲时长（外部写入）

    // 解码 / 渲染墙钟基准（最近一次）
    std::chrono::steady_clock::time_point lastDecodeTime;

    bool haveDecodeTime = false;

    // 抖动平滑因子（EWMA alpha；0.25 = 新样本权重）
    static constexpr double JITTER_ALPHA = 0.25;

    // 网络延迟 = jitter × 系数（保守估算，无发送端时钟）
    static constexpr double NETWORK_LATENCY_FACTOR = 3.0;
};

#pragma once

// ============================================================
// NetworkStatistics - 网络流统计（7.2 / 9.0 增强）
//
// 统计（每秒结算一次滑动窗口）：
//   - 输入 FPS   ：解码器每秒产出的帧数
//   - 输出 FPS   ：渲染线程每秒显示的帧数
//   - 码率       ：每秒收到的字节数 -> kbps
//   - 丢包率     ：因缓冲满丢弃 / 总接收
//   - 缓冲状态   ：当前缓冲包数 / 上限 / 是否饥饿（Buffering）
//   - 延迟       ：端到端延迟（毫秒，估算组装）
//
// 9.0 增强（评审意见）：
//   - 抖动（jitter）：包到达间隔 EWMA（RFC3550 风格）
//   - 吞吐量（kbps，与码率同源，独立命名）
//   - 延迟分量：解码延迟 / 渲染延迟 / 缓冲延迟（外部测量写入）
//   - 端到端延迟估算：网络估算（3×jitter）+ 各分量
//   - 卡顿统计：卡顿次数 / 累计时长
//   - 渲染丢帧统计：丢帧数 / 丢帧率
//   - 延迟方差（抖动 EWMA 的历史方差）
//
// OSD 显示示例：
//   RTSP Delay : 120 ms (Jit 8ms, Dec 12ms, Rdr 5ms)
//   Packet Loss: 0.2%
//   Buffer     : 45/600
//   FPS        : In 30.0 / Out 29.8
//
// 线程安全：任何线程可调用（内部互斥锁 + 原子量）。
// ============================================================

#include <string>
#include <mutex>
#include <atomic>
#include <chrono>

class NetworkStatistics
{
public:

    NetworkStatistics();

    // 清空全部统计
    void Reset();

    // ---------- 输入侧（Demux 线程） ----------

    // 收到一个网络包（bytes 为该包字节数）
    void OnPacketReceived(
        int bytes);

    // 因缓冲满丢了一个包（NetworkBuffer）
    // 网络丢包（队列满被丢弃，含 GOP 段丢包）
    void OnPacketDropped(
        int64_t count = 1);

    // 解码出一帧（输入 FPS）
    void OnFrameDecoded();

    // ---------- 输出侧（渲染线程） ----------

    // 渲染了一帧（输出 FPS）
    void OnFrameRendered();

    // 渲染线程因同步/延迟丢了一帧（渲染丢帧统计）
    void OnVideoFrameDropped();

    // ---------- 卡顿（渲染线程） ----------

    // 进入卡顿（缓冲饥饿开始）
    void OnStallStart();

    // 卡顿结束
    void OnStallEnd();

    // ---------- 缓冲 / 延迟 ----------

    // 更新缓冲水位（包数 / 上限）
    void SetBufferLevel(
        int packets,
        int maxPackets);

    // 设置端到端延迟（毫秒，兼容旧接口；
    // 9.0 起 Player 传估算组装值，未设置时内部自行组装）
    void SetLatencyMs(
        int ms);

    // 9.0：延迟分量（毫秒，由 LatencyEstimator 测量写入）
    void SetDecodeLatencyMs(
        double ms);

    void SetRenderLatencyMs(
        double ms);

    void SetBufferLatencyMs(
        double ms);

    // ---------- 读取 ----------

    double GetInputFps() const;

    double GetOutputFps() const;

    double GetBitrateKbps() const;

    // 丢包率（百分比，0.0 ~ 100.0）
    double GetPacketLossPercent() const;

    int GetBufferLevel() const;

    int GetBufferMax() const;

    int GetLatencyMs() const;

    // 是否处于饥饿状态（缓冲为空，等待网络数据）
    bool IsBuffering() const;

    // ---------- 9.0 增强读取 ----------

    // 网络抖动（毫秒，包到达间隔 EWMA）
    double GetJitterMs() const;

    // 吞吐量（kbps）
    double GetThroughputKbps() const;

    // 解码队列延迟（毫秒）
    double GetDecodeLatencyMs() const;

    // 渲染延迟（毫秒）
    double GetRenderLatencyMs() const;

    // 缓冲延迟（毫秒）
    double GetBufferLatencyMs() const;

    // 估算端到端延迟（毫秒）：
    //   显式 SetLatencyMs > 0 时返回显式值；
    //   否则 = 网络估算（3×jitter）+ decode + render + buffer
    double GetEndToEndLatencyMs() const;

    // 延迟方差（毫秒²，抖动 EWMA 的历史方差）
    double GetLatencyVariance() const;

    // 卡顿次数
    int GetStallCount() const;

    // 卡顿累计时长（毫秒）
    double GetStallDurationMs() const;

    // 渲染丢帧数 / 丢帧率（百分比 0~100，丢帧数 / 总渲染帧数）
    int GetDroppedFrameCount() const;

    double GetDropFrameRate() const;

    // 一行摘要，例如：
    // "Net | In 30.0fps | Out 29.8fps | 4200kbps | Loss 0.2% | Buf 45/600 | 120ms | Jit 8ms"
    std::string ToString() const;

private:

    // 每秒结算一次滑动窗口
    void Tick();

    // 记录包到达间隔（抖动 EWMA，调用者须已持有 mutex）
    void UpdateJitterLocked();

    mutable std::mutex mutex;      // 保护窗口计数

    // 滑动窗口计数（每秒清零重算）
    std::chrono::steady_clock::time_point lastTick;

    int windowPackets = 0;         // 窗口内接收包数

    int64_t windowBytes = 0;       // 窗口内接收字节数

    int windowDropped = 0;         // 窗口内丢弃包数

    int windowDecoded = 0;         // 窗口内解码帧数

    int windowRendered = 0;        // 窗口内渲染帧数

    // 结算结果
    double inputFps = 0.0;

    double outputFps = 0.0;

    double bitrateKbps = 0.0;

    double lossPercent = 0.0;

    // 缓冲 / 延迟（原子量，任意线程可读写）
    std::atomic<int> bufferLevel{ 0 };

    std::atomic<int> bufferMax{ 0 };

    std::atomic<int> latencyMs{ 0 };

    std::atomic<bool> buffering{ false };

    // ---------- 9.0：抖动 / 延迟分量（mutex 保护） ----------

    std::chrono::steady_clock::time_point lastArrival;

    bool haveLastArrival = false;

    double avgIntervalMs = 0.0;    // 平均到达间隔（毫秒）

    double jitterMs = 0.0;         // 抖动（毫秒，EWMA）

    double jitterVariance = 0.0;   // 延迟方差（抖动变化量 EWMA）

    double prevJitterMs = 0.0;     // 上次抖动值（方差计算用）

    double decodeLatencyMs = 0.0;  // 解码延迟（毫秒）

    double renderLatencyMs = 0.0;  // 渲染延迟（毫秒）

    double bufferLatencyMs = 0.0;  // 缓冲延迟（毫秒）

    // 抖动 EWMA 平滑因子
    static constexpr double JITTER_ALPHA = 0.25;

    // 网络延迟估算系数（无发送端时钟时的保守估算）
    static constexpr double NETWORK_LATENCY_FACTOR = 3.0;

    // ---------- 9.0：卡顿统计（mutex 保护） ----------

    int stallCount = 0;            // 卡顿次数

    double stallDurationMs = 0.0;  // 卡顿累计时长（毫秒）

    bool inStall = false;          // 是否正在卡顿

    std::chrono::steady_clock::time_point stallStart;

    // ---------- 9.0：渲染丢帧统计（mutex 保护） ----------

    int windowDroppedFrames = 0;   // 窗口内渲染丢帧数

    int droppedFrameCount = 0;     // 累计渲染丢帧数

    int renderedTotal = 0;         // 累计渲染帧数

    double dropFrameRate = 0.0;    // 丢帧率（%，结算结果）
};

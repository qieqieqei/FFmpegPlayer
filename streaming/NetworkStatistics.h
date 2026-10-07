#pragma once

// ============================================================
// NetworkStatistics - 网络流统计（7.2）
//
// 统计（每秒结算一次滑动窗口）：
//   - 输入 FPS   ：解码器每秒产出的帧数
//   - 输出 FPS   ：渲染线程每秒显示的帧数
//   - 码率       ：每秒收到的字节数 -> kbps
//   - 丢包率     ：因缓冲满丢弃 / 总接收
//   - 缓冲状态   ：当前缓冲包数 / 上限 / 是否饥饿（Buffering）
//   - 延迟       ：端到端延迟（毫秒，由外部 RTCP/缓冲估算写入）
//
// OSD 显示示例：
//   RTSP Delay : 120 ms
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

    // ---------- 缓冲 / 延迟 ----------

    // 更新缓冲水位（包数 / 上限）
    void SetBufferLevel(
        int packets,
        int maxPackets);

    // 设置端到端延迟（毫秒）
    void SetLatencyMs(
        int ms);

    // ---------- 读取 ----------

    double GetInputFps() const;

    double GetOutputFps() const;

    double GetBitrateKbps() const;

    // 丢包率（百分比，0.0 ~ 100.0）
    double GetPacketLossPercent() const;

    int GetBufferLevel() const;

    int GetBufferMax() const;

    int GetLatencyMs() const;

    // 累计丢弃包数（自 Reset 以来，含 GOP 段丢包）
    int64_t GetDroppedPackets() const;

    // 是否处于饥饿状态（缓冲为空，等待网络数据）
    bool IsBuffering() const;

    // 一行摘要，例如：
    // "Net | In 30.0fps | Out 29.8fps | 4200kbps | Loss 0.2% | Buf 45/600 | 120ms"
    std::string ToString() const;

private:

    // 每秒结算一次滑动窗口
    void Tick();

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

    // 累计丢弃包数（v2 Metrics）
    std::atomic<int64_t> totalDropped{ 0 };
};

#pragma once

// ============================================================
// JitterBuffer - RTP 抖动缓冲（9.0，评审意见）
//
// 职责：吸收网络抖动，按 RTP 时间戳（32 位）排序缓存，
//       到期（<= 当前播放时刻）后按序投递给解码器。
//
// 行为：
//   - Add(ts, data)   : 按时间戳排序存入（multimap）
//   - GetNext(nowMs)  : 取出所有到期包（最早时间戳 <= nowMs）
//   - 深度控制        : 目标深度 targetMs（AdaptiveBufferController
//                       的输出），超过 maxMs 丢最旧（overrun）
//   - 饥饿统计        : 播放请求时缓存为空计一次 underrun
//
// 与 PacketReorderBuffer 的配合：重排恢复序号顺序后，本类负责
// 时序（时间戳）层面的抖动吸收；时间戳 -> 毫秒换算按媒体时钟
// （默认 90000Hz，可配置）。
//
// 线程安全：单线程消费（调用方保证互斥，与 ReorderBuffer 同线程）。
// ============================================================

#include <cstdint>
#include <map>
#include <vector>

class JitterBuffer
{
public:

    JitterBuffer();

    // ---------- 配置 ----------

    // 目标播放深度（毫秒，默认 100；AdaptiveBufferController 输出）
    void SetTargetMs(
        int ms);

    // 深度上限（毫秒，默认 500；超出丢最旧）
    void SetMaxMs(
        int ms);

    // 媒体时间基准（RTP 时间戳每秒 tick 数，默认 90000）
    void SetTimeBase(
        int num,
        int den);

    // ---------- 数据 ----------

    // 添加一个 RTP 包（按时间戳排序存储）
    void Add(
        std::uint32_t timestamp,
        const std::uint8_t* data,
        int size);

    // 取一个到期包（nowMs：当前播放时刻，steady 毫秒）。
    // 有到期包返回 true 且 out 填充；无到期包返回 false。
    bool GetNext(
        std::int64_t nowMs,
        std::vector<std::uint8_t>& out);

    // 清理超限：深度超过 maxMs 时丢弃最旧包（返回丢弃包数）
    int TrimOverrun();

    // 清空所有状态
    void Reset();

    // ---------- 状态 ----------

    // 当前缓存包数
    int GetPacketCount() const;

    // 播放深度（毫秒：最新 - 最早时间戳换算）
    int GetDepthMs() const;

    // 目标深度（毫秒）
    int GetTargetMs() const;

    // 累计饥饿次数（播放请求时缓存为空）
    std::int64_t GetUnderrunCount() const;

    // 累计溢出丢包次数
    std::int64_t GetOverrunCount() const;

    // 累计主动丢弃包数（overrun / 清空）
    std::int64_t GetDroppedCount() const;

private:

    // RTP 时间戳 -> 毫秒（按媒体时钟）
    std::int64_t TsToMs(
        std::uint32_t ts) const;

    std::multimap<std::uint32_t, std::vector<std::uint8_t>> packets;

    int targetMs = 100;         // 目标播放深度（毫秒）

    int maxMs = 500;            // 深度上限（毫秒）

    int tbNum = 90000;          // 媒体时钟分子

    int tbDen = 1;              // 媒体时钟分母

    std::int64_t underrunCount = 0; // 饥饿次数

    std::int64_t overrunCount = 0;  // 溢出丢包次数

    std::int64_t droppedCount = 0;  // 主动丢弃包数
};

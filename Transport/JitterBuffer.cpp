// ============================================================
// JitterBuffer.cpp - RTP 抖动缓冲（9.0）
//
// 时间戳换算：RTP 时间戳单位 = 1 / 媒体时钟 秒
//   ms = ts * 1000 * tbDen / tbNum
//   （默认 90000Hz：一秒的 RTP 时间戳跨度 = 90000 tick）
// 使用 int64 中间量避免 32 位溢出。
// ============================================================

#include "Transport/JitterBuffer.h"

void JitterBuffer::SetTargetMs(
    int ms)
{
    targetMs =
        ms > 0 ?
        ms :
        100;
}

void JitterBuffer::SetMaxMs(
    int ms)
{
    maxMs =
        ms > 0 ?
        ms :
        500;
}

void JitterBuffer::SetTimeBase(
    int num,
    int den)
{
    if (num > 0 &&
        den > 0)
    {
        tbNum = num;

        tbDen = den;
    }
}

void JitterBuffer::Add(
    std::uint32_t timestamp,
    const std::uint8_t* data,
    int size)
{
    if (!data ||
        size <= 0)
    {
        return;
    }

    packets.insert(
        std::make_pair(
            timestamp,
            std::vector<std::uint8_t>(
                data,
                data + size)));

    // 缓存超限：立即丢最旧（保持有界内存）
    while (GetDepthMs() > maxMs &&
        !packets.empty())
    {
        packets.erase(packets.begin());

        ++overrunCount;

        ++droppedCount;
    }
}

bool JitterBuffer::GetNext(
    std::int64_t nowMs,
    std::vector<std::uint8_t>& out)
{
    if (packets.empty())
    {
        // 播放请求时缓存为空：计一次饥饿
        ++underrunCount;

        return false;
    }

    std::uint32_t firstTs =
        packets.begin()->first;

    if (TsToMs(firstTs) > nowMs)
    {
        // 最早包尚未到期：正常抖动等待，不计数
        return false;
    }

    // 取最早包
    out = std::move(
        packets.begin()->second);

    packets.erase(packets.begin());

    return true;
}

int JitterBuffer::TrimOverrun()
{
    int dropped = 0;

    while (GetDepthMs() > maxMs &&
        !packets.empty())
    {
        packets.erase(packets.begin());

        ++overrunCount;

        ++droppedCount;

        ++dropped;
    }

    return dropped;
}

void JitterBuffer::Reset()
{
    packets.clear();

    underrunCount = 0;

    overrunCount = 0;

    droppedCount = 0;
}

int JitterBuffer::GetPacketCount() const
{
    return static_cast<int>(packets.size());
}

int JitterBuffer::GetDepthMs() const
{
    if (packets.size() < 2)
    {
        return 0;
    }

    std::uint32_t firstTs =
        packets.begin()->first;

    std::uint32_t lastTs =
        packets.rbegin()->first;

    // 32 位回绕安全差值（真实播放跨度远小于 2^32 tick）
    std::int64_t span =
        static_cast<std::int64_t>(
            static_cast<std::uint32_t>(
                lastTs - firstTs));

    return static_cast<int>(
        TsToMs(
            static_cast<std::uint32_t>(span)));
}

int JitterBuffer::GetTargetMs() const
{
    return targetMs;
}

std::int64_t JitterBuffer::GetUnderrunCount() const
{
    return underrunCount;
}

std::int64_t JitterBuffer::GetOverrunCount() const
{
    return overrunCount;
}

std::int64_t JitterBuffer::GetDroppedCount() const
{
    return droppedCount;
}

std::int64_t JitterBuffer::TsToMs(
    std::uint32_t ts) const
{
    return static_cast<std::int64_t>(ts) *
        1000LL *
        tbDen /
        tbNum;
}

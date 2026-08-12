// ============================================================
// PacketReorderBuffer.cpp - RTP 序号重排缓冲（9.0）
//
// 序号比较用 int16_t 差值：
//   diff = (int16_t)(seq - expected)
//   diff > 0 : seq 在期望之后（缺口）
//   diff < 0 : seq 在期望之前（迟到）
// 16 位回绕（65535 -> 0）自动安全。
// ============================================================

#include "Transport/PacketReorderBuffer.h"

PacketReorderBuffer::PacketReorderBuffer()
{
}

void PacketReorderBuffer::SetMaxDepth(
    int packets)
{
    maxDepth =
        packets > 0 ?
        packets :
        64;
}

void PacketReorderBuffer::SetMaxWaitMs(
    int ms)
{
    maxWaitMs =
        ms >= 0 ?
        ms :
        50;
}

bool PacketReorderBuffer::Add(
    std::uint16_t seq,
    const std::uint8_t* data,
    int size)
{
    if (!data ||
        size <= 0)
    {
        return false;
    }

    // ---------- 首包：直接作为期望起点 ----------

    if (!haveExpected)
    {
        haveExpected = true;

        expectedSeq = seq;

        pending[seq] =
            std::vector<std::uint8_t>(
                data,
                data + size);

        return true;
    }

    // ---------- 序号比较 ----------

    std::int16_t diff =
        static_cast<std::int16_t>(
            seq - expectedSeq);

    if (diff < 0)
    {
        // 迟到包：已错过投递窗口，丢弃
        ++lateCount;

        return false;
    }

    if (diff == 0)
    {
        // 期望包：加入缓存（GetNext 会立即取出）
        pending[seq] =
            std::vector<std::uint8_t>(
                data,
                data + size);

        return true;
    }

    // ---------- 缺口（seq > expected） ----------

    // 深度超限：丢最旧的等待包（释放空间）
    if (static_cast<int>(pending.size()) >= maxDepth)
    {
        ++dropCount;

        pending.erase(pending.begin());

        // 若被丢的正是缺口起点，缺口顺延
        if (pending.empty() ||
            pending.begin()->first != expectedSeq)
        {
            gapStartMs = -1;
        }

        return false;
    }

    pending[seq] =
        std::vector<std::uint8_t>(
            data,
            data + size);

    ++reorderCount;

    // 缺口起始时刻由 FlushStale 在首次调用时记录（Add 不设值，
    // 避免与 "计时未开始" 状态（-1）混淆）
    return true;
}

const std::vector<std::uint8_t>* PacketReorderBuffer::GetNext()
{
    if (pending.empty())
    {
        return nullptr;
    }

    // 缓存首元素不是期望序号：缺口未填，无可投递
    if (pending.begin()->first != expectedSeq)
    {
        return nullptr;
    }

    // 连续包链：取出并推进期望序号
    std::uint16_t seq =
        pending.begin()->first;

    std::vector<std::uint8_t>& pkt =
        pending.begin()->second;

    // 先拷贝再擦除，保证指针在擦除前有效
    static thread_local std::vector<std::uint8_t> deliver;

    deliver = std::move(pkt);

    pending.erase(pending.begin());

    expectedSeq =
        static_cast<std::uint16_t>(seq + 1);

    // 缺口已填平
    if (pending.empty() ||
        pending.begin()->first == expectedSeq)
    {
        gapStartMs = -1;
    }

    return &deliver;
}

void PacketReorderBuffer::FlushStale(
    std::int64_t nowMs)
{
    if (!haveExpected ||
        pending.empty())
    {
        gapStartMs = -1;

        return;
    }

    if (!HasGap())
    {
        gapStartMs = -1;

        return;
    }

    // 缺口起始时刻：首次清理调用时记录，之后按等待时长判定
    if (gapStartMs < 0)
    {
        gapStartMs = nowMs;

        return;
    }

    if (nowMs - gapStartMs < maxWaitMs)
    {
        return; // 缺口仍在等待窗口内
    }

    // 超时：跳过缺失包，把期望推进到缓存首元素
    std::uint16_t next =
        pending.begin()->first;

    dropCount +=
        static_cast<std::int64_t>(
            static_cast<std::int16_t>(next - expectedSeq));

    expectedSeq = next;

    gapStartMs = -1;
}

void PacketReorderBuffer::Reset()
{
    pending.clear();

    expectedSeq = 0;

    haveExpected = false;

    gapStartMs = -1;

    reorderCount = 0;

    lateCount = 0;

    dropCount = 0;
}

int PacketReorderBuffer::GetBufferedCount() const
{
    return static_cast<int>(pending.size());
}

std::uint16_t PacketReorderBuffer::GetExpectedSeq() const
{
    return expectedSeq;
}

std::int64_t PacketReorderBuffer::GetReorderCount() const
{
    return reorderCount;
}

std::int64_t PacketReorderBuffer::GetLateCount() const
{
    return lateCount;
}

std::int64_t PacketReorderBuffer::GetDropCount() const
{
    return dropCount;
}

bool PacketReorderBuffer::HasGap() const
{
    if (pending.empty())
    {
        return false;
    }

    return pending.begin()->first != expectedSeq;
}

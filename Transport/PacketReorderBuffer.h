#pragma once

// ============================================================
// PacketReorderBuffer - RTP 序号重排缓冲（9.0，评审意见）
//
// 职责：吸收网络乱序，按 RTP 序号（16 位，回绕安全）恢复
//       原始顺序后再投递给上层（JitterBuffer / 解码器）。
//
// 行为：
//   - 序号 == 期望序号：立即投递（连续包链）
//   - 序号 > 期望序号 ：缺口 -> 缓存等待（统计 reorder）
//   - 序号 < 期望序号 ：迟到包，直接丢弃（统计 late）
//   - 缺口超时（maxWaitMs）：跳过缺失包，从缓存继续（统计 drop）
//   - 缓存超限（maxDepth） ：丢最旧等待包（统计 drop）
//
// 线程安全：单线程消费（传输线程 Add / 播放线程 GetNext），
// 调用方自行保证互斥（与 JitterBuffer 同线程协作）。
// ============================================================

#include <cstdint>
#include <map>
#include <vector>

class PacketReorderBuffer
{
public:

    PacketReorderBuffer();

    // ---------- 配置 ----------

    // 缓存深度上限（包数，默认 64）
    void SetMaxDepth(
        int packets);

    // 缺口最大等待时长（毫秒，默认 50）
    void SetMaxWaitMs(
        int ms);

    // ---------- 数据 ----------

    // 添加一个 RTP 包（按序号）。
    // 返回 true 表示已接收（可能立即投递或缓存）；
    // false 表示被拒绝（迟到 / 超限）。
    bool Add(
        std::uint16_t seq,
        const std::uint8_t* data,
        int size);

    // 取下一个连续包（无就绪包返回 nullptr）。
    // 返回的指针在下次调用 Add / GetNext / FlushStale 前有效。
    const std::vector<std::uint8_t>* GetNext();

    // 缺口超时清理：等待超过 maxWaitMs 的缺口直接跳过，
    // 使缓存中的后续包变为连续。nowMs 为 steady 时钟毫秒。
    void FlushStale(
        std::int64_t nowMs);

    // 清空所有状态
    void Reset();

    // ---------- 状态 ----------

    // 当前缓存包数
    int GetBufferedCount() const;

    // 期望的下一个序号（调试用）
    std::uint16_t GetExpectedSeq() const;

    // 累计乱序到达包数（序号 > 期望）
    std::int64_t GetReorderCount() const;

    // 累计迟到包数（序号 < 期望，被丢弃）
    std::int64_t GetLateCount() const;

    // 累计跳过的缺口包数（超时 / 超限）
    std::int64_t GetDropCount() const;

private:

    // 是否还有待处理缺口
    bool HasGap() const;

    std::map<std::uint16_t, std::vector<std::uint8_t>> pending;

    std::uint16_t expectedSeq = 0;  // 期望的下一个序号

    bool haveExpected = false;      // 是否已收到首包

    int maxDepth = 64;              // 缓存深度上限

    int maxWaitMs = 50;             // 缺口最大等待时长

    std::int64_t gapStartMs = -1;   // 缺口起始时刻（-1 = 无缺口）

    std::int64_t reorderCount = 0;  // 乱序到达包数

    std::int64_t lateCount = 0;     // 迟到包数

    std::int64_t dropCount = 0;     // 跳过包数
};

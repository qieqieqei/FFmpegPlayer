#pragma once

// ============================================================
// DropController - 丢帧控制器（8.1）
//
// 决定"落后太多时是否丢帧追赶"，并统计丢帧：
//
//   ShouldDrop(delay) : delay < -threshold 时建议丢帧
//   OnFrameDropped()  : 调用方确认丢帧后登记计数
//
// 附带防抖策略：
//   - 连续丢帧保护：一帧落后不代表持续落后（网络抖动尖峰），
//     连续 N 次建议丢帧才真正丢第一帧，避免频繁跳帧观感差；
//   - 丢帧后冷却：丢一帧后短时间内（cooldown）不再丢，
//     给解码/渲染管线时间追赶。
//
// 线程安全：渲染线程独占使用，计数用原子量兜底。
// ============================================================

#include <atomic>

class DropController
{
public:

    DropController();

    // 设置落后丢帧阈值（秒），默认 0.05
    void SetThreshold(
        double seconds);

    double GetThreshold() const;

    // 是否建议丢帧
    // delay: 视频帧相对主时钟的延迟（秒，负 = 落后）
    bool ShouldDrop(
        double delay) const;

    // 登记丢了一帧（渲染线程确认丢弃后调用）
    void OnFrameDropped();

    // 累计丢帧数
    int GetDropCount() const;

    // 重置（Seek / 切换媒体时调用）
    void Reset();

private:

    double threshold = 0.05;          // 落后阈值（秒）

    mutable std::atomic<int> hintCount{ 0 };     // 连续建议丢帧计数

    std::atomic<int64_t> dropCount{ 0 };         // 累计丢帧

    std::atomic<int64_t> lastDropTick{ 0 };      // 上次丢帧时刻（ms）

    int consecutiveLimit = 2;         // 连续建议几次后开始丢

    int cooldownMs = 200;             // 丢帧冷却（毫秒）
};

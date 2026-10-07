#pragma once

// ============================================================
// LiveClock - 直播同步策略（8.5）
//
// 直播和文件播放的同步目标不同：
//
//   文件播放：PTS 正常对齐——视频超前就等待，落后就丢帧，
//            目的是"完整、流畅地播完"
//   直播     ：目标是【最低延迟】——不等待、超阈值就丢帧，
//            始终追最新画面
//
// 策略（delay = videoPts - masterTime）：
//
//   delay >  +aheadMs   : 视频超前太多（积压了延迟，例：网络延迟 1s）
//                          -> 丢帧追赶
//   delay <  -behindMs  : 视频落后太多（卡顿）-> 丢帧追赶
//   其余                  -> 不丢帧，立即渲染（不等待）
//
// 例：网络延迟 1 秒
//     delay ≈ 1s > aheadMs -> 丢帧
//     -> 下一帧离实时更近 -> 循环 -> 恢复实时
//
// 防抖（与 DropController 一致）：
//   - 连续建议计数：连续 N 次超阈值才真正丢，避免单帧抖动误丢
//   - 冷却：丢帧后短时间内不再丢，给管线时间追赶
//
// 线程归属：渲染线程独占（与 DropController 一致），计数原子兜底。
// ============================================================

#include <atomic>

class LiveClock
{
public:

    LiveClock();

    // 超前丢帧阈值（毫秒）：视频比主时钟超前超过该值就丢帧。
    // 默认 100ms——比 DropController 的 0.05s 宽松，
    // 直播允许轻微超前（画面比音频稍快不突兀），
    // 但一旦积压（如 1s 网络延迟）立即开始丢帧追赶。
    void SetAheadThresholdMs(
        int ms);

    int GetAheadThresholdMs() const;

    // 落后丢帧阈值（毫秒）：视频落后超过该值就丢帧。
    // 默认 50ms（与 DropController 一致）。
    void SetBehindThresholdMs(
        int ms);

    int GetBehindThresholdMs() const;

    // 是否应丢帧（直播策略：超前 / 落后都丢，防抖后判定）
    bool ShouldDrop(
        double delay) const;

    // 登记丢了一帧（渲染线程丢弃后调用）
    void OnFrameDropped();

    // 累计丢帧数
    int GetDropCount() const;

    // 重置（切换媒体 / Seek 时调用）
    void Reset();

private:

    int aheadThresholdMs = 100;    // 超前丢帧阈值（毫秒）

    int behindThresholdMs = 50;    // 落后丢帧阈值（毫秒）

    // 连续建议计数：连续 consecutiveLimit 次超阈值才丢
    mutable std::atomic<int> hintCount{ 0 };

    // 冷却（毫秒）：丢帧后短时间内不再丢
    int cooldownMs = 100;

    int consecutiveLimit = 2;      // 连续超阈值次数上限

    std::atomic<int64_t> dropCount{ 0 };      // 累计丢帧数

    std::atomic<int64_t> lastDropTick{ 0 };   // 上次丢帧时刻（SDL tick）
};

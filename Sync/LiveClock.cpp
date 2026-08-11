#include "Sync/LiveClock.h"

#include <SDL.h>

#include <algorithm>

LiveClock::LiveClock()
{
}

void LiveClock::SetAheadThresholdMs(
    int ms)
{
    if (ms > 0)
    {
        aheadThresholdMs = ms;
    }
}

int LiveClock::GetAheadThresholdMs() const
{
    return aheadThresholdMs;
}

void LiveClock::SetBehindThresholdMs(
    int ms)
{
    if (ms > 0)
    {
        behindThresholdMs = ms;
    }
}

int LiveClock::GetBehindThresholdMs() const
{
    return behindThresholdMs;
}

bool LiveClock::ShouldDrop(
    double delay) const
{
    // 直播策略（8.5）：超前 / 落后都丢帧，追最新画面
    //
    //   delay >  +aheadMs : 视频超前太多（网络延迟积压）
    //   delay <  -behindMs: 视频落后太多（卡顿）
    //   其余               : 立即渲染（不等待）
    double aheadSec =
        aheadThresholdMs / 1000.0;

    double behindSec =
        behindThresholdMs / 1000.0;

    if (delay <= aheadSec &&
        delay >= -behindSec)
    {
        // 正常范围：不丢
        return false;
    }

    // 冷却中：不丢（给管线时间追赶）
    if (cooldownMs > 0)
    {
        int64_t now =
            static_cast<int64_t>(
                SDL_GetTicks64());

        if (now - lastDropTick.load() < cooldownMs)
        {
            return false;
        }
    }

    // 连续建议计数：连续 N 次超阈值才真正丢，
    // 避免单帧抖动尖峰触发跳帧
    int hint =
        hintCount.fetch_add(1) + 1;

    if (hint < consecutiveLimit)
    {
        return false;
    }

    return true;
}

void LiveClock::OnFrameDropped()
{
    dropCount.fetch_add(1);

    lastDropTick.store(
        static_cast<int64_t>(
            SDL_GetTicks64()));

    // 重置连续建议计数
    hintCount.store(0);
}

int LiveClock::GetDropCount() const
{
    return static_cast<int>(
        dropCount.load());
}

void LiveClock::Reset()
{
    hintCount.store(0);

    dropCount.store(0);

    lastDropTick.store(0);
}

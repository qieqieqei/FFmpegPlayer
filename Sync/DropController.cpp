#include "Sync/DropController.h"

#include <SDL.h>

#include <algorithm>

DropController::DropController()
{
}

void DropController::SetThreshold(
    double seconds)
{
    if (seconds > 0.0)
    {
        threshold = seconds;
    }
}

double DropController::GetThreshold() const
{
    return threshold;
}

bool DropController::ShouldDrop(
    double delay) const
{
    // 未落后：不丢
    if (delay >= -threshold)
    {
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

    // 连续建议计数：连续 N 次落后才真正丢，
    // 避免单次网络抖动尖峰触发跳帧
    int hint =
        hintCount.fetch_add(1) + 1;

    if (hint < consecutiveLimit)
    {
        return false;
    }

    return true;
}

void DropController::OnFrameDropped()
{
    dropCount.fetch_add(1);

    lastDropTick.store(
        static_cast<int64_t>(
            SDL_GetTicks64()));

    // 重置连续建议计数
    hintCount.store(0);
}

int DropController::GetDropCount() const
{
    return static_cast<int>(
        dropCount.load());
}

void DropController::Reset()
{
    hintCount.store(0);

    dropCount.store(0);

    lastDropTick.store(0);
}

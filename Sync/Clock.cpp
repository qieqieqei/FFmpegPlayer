#include "Sync/Clock.h"

#include <SDL.h>

#include <algorithm>

Clock::Clock()
{
    Reset();
}

void Clock::SetClock(
    double pts)
{
    this->pts = pts;

    lastUpdateTime =
        SDL_GetTicks() /
        1000.0;
}

double Clock::GetClock() const
{
    double now =
        SDL_GetTicks() /
        1000.0;

    // 从基准时刻到现在流逝的时间
    double elapsed =
        std::max(
            0.0,
            now - lastUpdateTime);

    return pts + elapsed;
}

void Clock::Reset()
{
    pts = 0.0;

    lastUpdateTime =
        SDL_GetTicks() /
        1000.0;
}

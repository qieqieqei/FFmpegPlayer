#include "Sync/SyncController.h"

#include "Utils/ErrorHandler.h"

#include <SDL.h>

SyncController::SyncController()
{
    Reset();
}

double SyncController::GetVideoDelay(
    double videoPts,
    double audioPts)
{
    // 更新内部时钟
    videoClock.SetClock(videoPts);

    audioClock.SetClock(audioPts);

    // 视频超前为正，落后为负
    double delay =
        videoClock.GetClock() -
        audioClock.GetClock();

    if (delay < -0.5 || delay > 1.0)
    {
        // 偏差过大，可能是 Seek 或刚启动
        // 限制每秒最多打印一次，避免刷屏
        static Uint32 lastLogMs = 0;

        Uint32 nowMs = SDL_GetTicks();

        if (nowMs - lastLogMs > 1000)
        {
            lastLogMs = nowMs;

            ErrorHandler::Log(
                ErrorTag::Sync,
                "Large sync offset : " +
                std::to_string(delay) +
                " s");
        }
    }

    return delay;
}

double SyncController::GetDropThreshold() const
{
    return dropThreshold;
}

void SyncController::Reset()
{
    audioClock.Reset();

    videoClock.Reset();
}

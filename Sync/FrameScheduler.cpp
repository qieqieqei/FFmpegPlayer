#include "Sync/FrameScheduler.h"

#include <algorithm>
#include <cmath>

FrameScheduler::FrameScheduler()
{
}

double FrameScheduler::ComputeDelay(
    double videoPts,
    double masterTime) const
{
    // 视频超前为正，落后为负
    return videoPts - masterTime;
}

double FrameScheduler::ClampDelay(
    double delay,
    bool* isAbnormal) const
{
    if (isAbnormal)
    {
        *isAbnormal =
            delay > maxAheadSec ||
            delay < -maxBehindSec;
    }

    return std::max(
        -maxBehindSec,
        std::min(
            maxAheadSec,
            delay));
}

double FrameScheduler::ComputeFrameInterval(
    double frameDuration,
    double speed) const
{
    if (frameDuration <= 0.0)
    {
        frameDuration = 1.0 / 25.0;
    }

    double s =
        speed > 0.0 ?
        speed :
        1.0;

    return frameDuration / s;
}

int FrameScheduler::NextWaitMs(
    double delay) const
{
    if (delay <= 0.0)
    {
        return 0;
    }

    double waitMs =
        delay * 1000.0;

    // 分片：一次最多 waitChunkMs，剩余下次再等
    if (waitMs > waitChunkMs)
    {
        waitMs = waitChunkMs;
    }

    return static_cast<int>(
        std::ceil(waitMs));
}

void FrameScheduler::SetMaxAheadSec(
    double sec)
{
    if (sec > 0.0)
    {
        maxAheadSec = sec;
    }
}

void FrameScheduler::SetMaxBehindSec(
    double sec)
{
    if (sec > 0.0)
    {
        maxBehindSec = sec;
    }
}

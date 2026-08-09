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

double FrameScheduler::ComputeTargetDelay(
    double delay,
    double diff,
    double frameDuration) const
{
    // 帧时长无效（未知帧率）时不调整
    if (frameDuration <= 0.0)
    {
        return delay;
    }

    // 同步阈值：随帧时长在 [0.04, 0.1] 区间缩放（ffplay）
    double syncThreshold =
        std::max(
            0.04,
            std::min(
                0.1,
                frameDuration));

    // 帧重复保护阈值（ffplay AV_SYNC_FRAMEDUP_THRESHOLD）
    const double kFrameDupThreshold = 0.1;

    // 最大帧时长：偏差超过该值视为异常跳变（Seek / 断流），不调整
    const double kMaxFrameDuration = 1.0;

    if (diff < -kMaxFrameDuration ||
        diff > kMaxFrameDuration)
    {
        return delay;
    }

    if (diff <= -syncThreshold)
    {
        // 视频落后：缩短等待（最快立即显示）追赶音频
        delay =
            std::max(
                0.0,
                delay + diff);
    }
    else if (diff >= syncThreshold)
    {
        // 视频领先：放慢显示等音频
        if (delay > kFrameDupThreshold)
        {
            // 延迟已较大：直接加偏差（防帧重复积压）
            delay += diff;
        }
        else
        {
            // 延迟小：翻倍（轻微领先时平滑放慢）
            delay *= 2.0;
        }
    }

    return delay;
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

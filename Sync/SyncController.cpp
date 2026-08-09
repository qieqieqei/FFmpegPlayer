#include "Sync/SyncController.h"

#include "Sync/AudioClock.h"

#include "Utils/ErrorHandler.h"

#include <SDL.h>

SyncController::SyncController()
{
    Reset();

    // 视频时钟接入主时钟选择器
    masterClock.SetVideoClock(
        &videoClock);
}

void SyncController::SetAudioClock(
    AudioClock* clock)
{
    masterClock.SetAudioClock(
        clock);
}

double SyncController::GetMasterTime() const
{
    return masterClock.GetTime();
}

double SyncController::GetVideoDelay(
    double videoPts,
    double frameDuration)
{
    // 主时钟时间 = 音频时钟（有音频）或视频时钟（无音频）
    double masterTime =
        masterClock.GetTime();

    double delay =
        frameScheduler.ComputeDelay(
            videoPts,
            masterTime);

    // 8.4（评审五）：ffplay 级目标延迟调整。
    // 渲染线程已用当前帧 PTS 推进视频时钟，
    // diff = 视频时钟 - 主时钟（正 = 视频领先）
    double diff =
        videoClock.Get() - masterTime;

    return frameScheduler.ComputeTargetDelay(
        delay,
        diff,
        frameDuration);
}

double SyncController::ClampDelay(
    double delay,
    bool* isAbnormal) const
{
    return frameScheduler.ClampDelay(
        delay,
        isAbnormal);
}

double SyncController::GetFrameInterval(
    double frameDuration) const
{
    // 速度由外部 SpeedController 管理（Player 持有），
    // 这里只算基础帧间隔，Player 会再除以速度
    return frameScheduler.ComputeFrameInterval(
        frameDuration,
        1.0);
}

int SyncController::NextWaitMs(
    double delay) const
{
    return frameScheduler.NextWaitMs(delay);
}

bool SyncController::ShouldDrop(
    double delay) const
{
    return dropController.ShouldDrop(delay);
}

void SyncController::OnFrameDropped()
{
    dropController.OnFrameDropped();
}

int SyncController::GetDropCount() const
{
    return dropController.GetDropCount();
}

double SyncController::GetDropThreshold() const
{
    return dropController.GetThreshold();
}

void SyncController::UpdateVideoClock(
    double pts)
{
    videoClock.SetPts(pts);
}

double SyncController::GetVideoClockTime() const
{
    return videoClock.Get();
}

VideoClock* SyncController::GetVideoClock()
{
    return &videoClock;
}

MasterClock* SyncController::GetMasterClock()
{
    return &masterClock;
}

FrameScheduler* SyncController::GetFrameScheduler()
{
    return &frameScheduler;
}

DropController* SyncController::GetDropController()
{
    return &dropController;
}

void SyncController::Reset()
{
    videoClock.Reset();

    masterClock.Reset();

    dropController.Reset();
}

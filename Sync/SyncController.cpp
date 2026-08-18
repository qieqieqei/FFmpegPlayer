#include "Sync/SyncController.h"

#include <climits>

#include "Sync/AudioClock.h"

#include "Utils/ErrorHandler.h"
#include "Utils/Logger.h"

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

int SyncController::GetAvSyncMs() const
{
    // 视频时钟 - 主时钟（音频优先；无音频时主时钟 = 视频时钟，差恒为 0）
    return static_cast<int>(
        (videoClock.Get() - masterClock.GetTime()) * 1000.0);
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
    // 8.5：直播模式不等待（最低延迟，追最新画面）
    if (liveMode)
    {
        return 0;
    }

    return frameScheduler.NextWaitMs(delay);
}

bool SyncController::ShouldDrop(
    double delay) const
{
    // 8.5：直播 / 点播走不同丢帧策略
    if (liveMode)
    {
        // 直播：超前（积压延迟）或落后都丢帧，追最新
        return liveClock.ShouldDrop(delay);
    }

    return dropController.ShouldDrop(delay);
}

void SyncController::OnFrameDropped()
{
    if (liveMode)
    {
        liveClock.OnFrameDropped();

        return;
    }

    dropController.OnFrameDropped();
}

int SyncController::GetDropCount() const
{
    if (liveMode)
    {
        return liveClock.GetDropCount();
    }

    return dropController.GetDropCount();
}

double SyncController::GetDropThreshold() const
{
    // 直播：返回落后阈值（毫秒转秒）
    if (liveMode)
    {
        return liveClock.GetBehindThresholdMs() / 1000.0;
    }

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

    liveClock.Reset();

    // 重置不改变模式（Player 每次 OpenMedia 会显式 SetLiveMode）
}

void SyncController::SetLiveMode(
    bool live)
{
    liveMode = live;

    // 切换模式时清掉旧的防抖计数 / 丢帧统计
    dropController.Reset();

    liveClock.Reset();

    Logger::Info()
        << "[Sync] Live mode : "
        << (live ? "on" : "off")
        << std::endl;
}

bool SyncController::IsLiveMode() const
{
    return liveMode;
}

void SyncController::SetBufferStableMode(
    bool stable)
{
    if (stable)
    {
        // 稳定缓冲：关超前丢帧（阈值极大，实际永不触发），
        // 落后丢帧 2500ms 兜底（防异常下缓冲无限增长）
        liveClock.SetAheadThresholdMs(INT_MAX - 1);

        liveClock.SetBehindThresholdMs(2500);
    }
    else
    {
        // 低延迟：恢复旧行为（超前 100ms / 落后 50ms）
        liveClock.SetAheadThresholdMs(100);

        liveClock.SetBehindThresholdMs(50);
    }

    Logger::Info()
        << "[Sync] Buffer stable mode : "
        << (stable ? "on (ahead-drop off, behind 2500ms)" : "off (low-latency 100/50ms)")
        << std::endl;
}

LiveClock* SyncController::GetLiveClock()
{
    return &liveClock;
}

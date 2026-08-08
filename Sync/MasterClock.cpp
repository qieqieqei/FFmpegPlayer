#include "Sync/MasterClock.h"

#include "Sync/AudioClock.h"
#include "Sync/VideoClock.h"

MasterClock::MasterClock()
{
}

void MasterClock::SetAudioClock(
    AudioClock* clock)
{
    audioClock = clock;
}

void MasterClock::SetVideoClock(
    VideoClock* clock)
{
    videoClock = clock;
}

void MasterClock::SetMode(
    Mode newMode)
{
    mode.store(newMode);
}

MasterClock::Mode MasterClock::GetMode() const
{
    return mode.load();
}

bool MasterClock::IsAudioMaster() const
{
    // 实际生效模式：
    //   Audio 强制 / Auto 且音频可用 -> 音频主时钟
    Mode m =
        mode.load();

    if (m == Mode::Audio)
    {
        return audioClock != nullptr;
    }

    if (m == Mode::Video)
    {
        return false;
    }

    // Auto
    return audioClock != nullptr;
}

double MasterClock::GetTime() const
{
    if (IsAudioMaster() &&
        audioClock)
    {
        return audioClock->Get();
    }

    if (videoClock)
    {
        return videoClock->Get();
    }

    return 0.0;
}

void MasterClock::Reset()
{
    // 注意：不重置音频时钟——音频时钟由 AudioDevice 管理，
    // Seek 时按目标时间 ResetClock(target) 单独处理；
    // 这里只重置视频时钟（主时钟选择器本身无状态）。
    if (videoClock)
    {
        videoClock->Reset();
    }
}

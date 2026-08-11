#include "Sync/AudioClock.h"

#include <chrono>
#include <cmath>

AudioClock::AudioClock()
{
}

void AudioClock::Reset(
    double baseSeconds)
{
    std::lock_guard<std::mutex> lock(mutex);

    // 参数与成员同名，必须 this-> 否则自赋值不生效
    // （曾导致 Seek 后时钟基准不更新）
    this->baseSeconds = baseSeconds;

    playedSeconds = 0.0;

    // 8.4：重置墙钟基准（漂移检测重新起算）
    baseWallSeconds = WallNow();

    wallInitialized = true;
}

void AudioClock::Update(
    double outputSeconds)
{
    if (outputSeconds <= 0.0)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex);

    playedSeconds += outputSeconds;
}

void AudioClock::SetSpeedFactor(
    double speed)
{
    speedFactor.store(speed);
}

double AudioClock::Get() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return baseSeconds +
        playedSeconds * speedFactor.load();
}

double AudioClock::GetWallDrift() const
{
    std::lock_guard<std::mutex> lock(mutex);

    if (!wallInitialized)
    {
        return 0.0;
    }

    double mediaTime =
        baseSeconds +
        playedSeconds * speedFactor.load();

    return mediaTime -
        (WallNow() - baseWallSeconds);
}

double AudioClock::CorrectDrift(
    double maxNudge)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (!wallInitialized)
    {
        return 0.0;
    }

    double mediaTime =
        baseSeconds +
        playedSeconds * speedFactor.load();

    double drift =
        mediaTime - (WallNow() - baseWallSeconds);

    // 小偏差（10ms 内）不动作，避免抖动
    if (std::abs(drift) < 0.010)
    {
        return 0.0;
    }

    // 每次修正偏差的 20%，单次不超过 maxNudge（默认 5ms）
    double nudge = drift * 0.2;

    if (nudge > maxNudge)
    {
        nudge = maxNudge;
    }
    else if (nudge < -maxNudge)
    {
        nudge = -maxNudge;
    }

    baseSeconds += nudge;

    return nudge;
}

double AudioClock::WallNow()
{
    auto now =
        std::chrono::steady_clock::now();

    return std::chrono::duration<double>(
        now.time_since_epoch()).count();
}

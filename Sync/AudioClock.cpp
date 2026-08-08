#include "Sync/AudioClock.h"

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

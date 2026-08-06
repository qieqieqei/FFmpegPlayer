#include "Sync/AudioClock.h"

AudioClock::AudioClock()
{
}

void AudioClock::Reset(
    double baseSeconds)
{
    std::lock_guard<std::mutex> lock(mutex);

    baseSeconds = baseSeconds;

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

#include "Audio/SpeedController.h"

SpeedController::SpeedController()
{
}

bool SpeedController::Init(
    int sampleRate,
    int channels)
{
    return audioSpeed.Init(
        sampleRate,
        channels);
}

void SpeedController::SetSpeed(
    double speed)
{
    this->speed.store(speed);

    // 同步给音频变速器
    audioSpeed.SetSpeed(speed);
}

double SpeedController::GetSpeed() const
{
    return speed.load();
}

double SpeedController::GetFrameDelay(
    double frameDuration) const
{
    double s = speed.load();

    if (s <= 0.01)
    {
        s = 1.0;
    }

    return frameDuration / s;
}

int SpeedController::Process(
    const uint8_t* in,
    int inBytes,
    uint8_t* out,
    int outCap)
{
    return audioSpeed.Process(
        in,
        inBytes,
        out,
        outCap);
}

int SpeedController::Flush(
    uint8_t* out,
    int outCap)
{
    return audioSpeed.Flush(
        out,
        outCap);
}

int SpeedController::PendingBytes() const
{
    return audioSpeed.PendingBytes();
}

void SpeedController::Reset()
{
    audioSpeed.Reset();
}

#include "Audio/SpeedController.h"

#include <algorithm>

// ============================================================
// SpeedController - 播放速度控制（6.6 / 9.0）
//
// 9.0（评审意见）：用户倍速与直播追帧倍速分离。
//   SetSpeed / SetUserSpeed : 用户主动倍速
//   SetChaseSpeed           : 直播追帧倍速（仅直播生效）
//   GetEffectiveSpeed       : 最终生效速度 = user × chase
// ============================================================

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
    // 兼容旧接口：用户倍速
    SetUserSpeed(speed);
}

double SpeedController::GetSpeed() const
{
    return rateController.GetUserSpeed();
}

void SpeedController::SetUserSpeed(
    double speed)
{
    rateController.SetUserSpeed(speed);

    // 同步给音频变速器（SOLA 内部按生效速度计算步长）
    audioSpeed.SetUserSpeed(speed);

    this->speed.store(
        GetEffectiveSpeed());
}

double SpeedController::GetUserSpeed() const
{
    return rateController.GetUserSpeed();
}

void SpeedController::SetChaseSpeed(
    double speed)
{
    rateController.SetChaseSpeed(speed);

    // 同步给音频变速器（追帧倍速只在直播模式由调用方下发；
    // 内部 SOLA 始终按 user × chase 计算）
    audioSpeed.SetChaseSpeed(speed);

    this->speed.store(
        GetEffectiveSpeed());
}

double SpeedController::GetChaseSpeed() const
{
    return rateController.GetChaseSpeed();
}

void SpeedController::SetLiveMode(
    bool live)
{
    rateController.SetLiveMode(live);

    // 切到点播：追帧倍速复位
    if (!live)
    {
        audioSpeed.SetChaseSpeed(1.0);
    }

    this->speed.store(
        GetEffectiveSpeed());
}

double SpeedController::GetEffectiveSpeed() const
{
    return rateController.GetEffectiveSpeed();
}

double SpeedController::GetFrameDelay(
    double frameDuration) const
{
    double s =
        rateController.GetEffectiveSpeed();

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

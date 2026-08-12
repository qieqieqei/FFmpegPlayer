#include "Sync/PlaybackRateController.h"

#include <algorithm>

// ============================================================
// PlaybackRateController - 播放速度合成控制器
// ============================================================

PlaybackRateController::PlaybackRateController()
{
}

void PlaybackRateController::SetLiveMode(
    bool live)
{
    this->live.store(live);

    // 切到点播：追帧倍速复位（避免残留）
    if (!live)
    {
        chaseSpeed.store(1.0);
    }
}

bool PlaybackRateController::IsLiveMode() const
{
    return live.load();
}

void PlaybackRateController::SetUserSpeed(
    double speed)
{
    speed =
        std::max(
            0.25,
            std::min(
                4.0,
                speed));

    userSpeed.store(speed);
}

double PlaybackRateController::GetUserSpeed() const
{
    return userSpeed.load();
}

void PlaybackRateController::SetChaseSpeed(
    double speed)
{
    speed =
        std::max(
            1.0,
            std::min(
                2.0,
                speed));

    chaseSpeed.store(speed);
}

double PlaybackRateController::GetChaseSpeed() const
{
    return chaseSpeed.load();
}

double PlaybackRateController::GetEffectiveSpeed() const
{
    double user = userSpeed.load();

    // 追帧倍速仅在直播模式生效
    double chase =
        live.load() ?
        chaseSpeed.load() :
        1.0;

    return
        std::max(
            0.25,
            std::min(
                4.0,
                user * chase));
}

void PlaybackRateController::Reset()
{
    userSpeed.store(1.0);

    chaseSpeed.store(1.0);
}

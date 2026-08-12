#pragma once

// ============================================================
// PlaybackRateController - 播放速度合成控制器（9.0）
//
// 背景（评审意见）：
//   用户主动倍速（1.5x）与直播追帧倍速（1.1x）必须分开管理，
//   否则会出现"1.5 × 1.1 = 1.65x"的错误叠加。
//
// 职责：合成最终生效速度
//
//   VOD  : effectiveSpeed = userSpeed
//   LIVE : effectiveSpeed = userSpeed × chaseSpeed
//
// 追帧倍速只在直播模式生效；点播模式恒为 1.0。
//
// 线程安全：全部原子量，任意线程可调用。
// ============================================================

#include <atomic>

class PlaybackRateController
{
public:

    PlaybackRateController();

    // 切换直播 / 点播（点播时 chase 不生效）
    void SetLiveMode(
        bool live);

    bool IsLiveMode() const;

    // 用户主动倍速（0.5 / 1.0 / 1.5 / 2.0，钳制 0.25 ~ 4.0）
    void SetUserSpeed(
        double speed);

    double GetUserSpeed() const;

    // 直播追帧倍速（LiveLatencyController 输出，钳制 1.0 ~ 2.0）
    void SetChaseSpeed(
        double speed);

    double GetChaseSpeed() const;

    // 最终生效速度：
    //   VOD  = userSpeed
    //   LIVE = userSpeed × chaseSpeed
    double GetEffectiveSpeed() const;

    // 重置（恢复 1.0，保留 live 模式——模式由调用方管理）
    void Reset();

private:

    std::atomic<bool> live{ false };      // 是否直播模式

    std::atomic<double> userSpeed{ 1.0 }; // 用户倍速

    std::atomic<double> chaseSpeed{ 1.0 }; // 追帧倍速（仅直播生效）
};

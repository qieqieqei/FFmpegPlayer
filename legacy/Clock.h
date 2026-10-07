#pragma once

// ============================================================
// Clock - 播放器时钟（5.1）
//
// 职责：
//   维护一个可查询的播放时钟。
//
// 原理：
//   SetClock(pts) 记录基准时间点
//   GetClock()    返回 基准pts + 从基准到现在流逝的时间
//
//   PTS: Presentation Time Stamp，显示时间戳（秒）
// ============================================================

class Clock
{
public:

    Clock();

    // 设置时钟
    // pts: 当前的显示时间戳（秒）
    void SetClock(
        double pts);

    // 获取当前时钟
    // 返回：pts + 自 SetClock 以来流逝的时间（秒）
    double GetClock() const;

    // 重置时钟
    void Reset();

private:

    double pts = 0.0;            // 基准时间戳（秒）

    double lastUpdateTime = 0.0; // 上次 SetClock 的时刻（秒，基于 SDL_GetTicks）
};

#pragma once

// ============================================================
// AudioClock - 音频主时钟（6.4）
//
// 以音频作为主时钟（Audio Clock Master）：
//
//   audio_clock = baseSeconds + playedSeconds * speedFactor
//
//   baseSeconds  : Seek 目标时间（时钟基准）
//   playedSeconds: 声卡实际输出的时长（回调里累加）
//   speedFactor  : 播放速度（倍速播放时媒体时间走得更快）
//
// 数据流：
//   SDL AudioCallback 每输出一段 PCM 就 Update() 一次，
//   渲染线程通过 Get() 查询当前媒体时间，
//   视频帧同步：delay = videoPts - audioClock
// ============================================================

#include <mutex>
#include <atomic>

class AudioClock
{
public:

    AudioClock();

    // 重置时钟（Seek 时调用）
    // baseSeconds: Seek 目标时间，作为新的时钟基准
    void Reset(
        double baseSeconds);

    // 推进时钟（SDL 回调里调用）
    // outputSeconds: 本次实际输出的音频时长（秒）
    void Update(
        double outputSeconds);

    // 设置播放速度（影响媒体时间换算）
    void SetSpeedFactor(
        double speed);

    // 获取当前媒体时间（秒）
    double Get() const;

    // ---------- 8.4：墙钟漂移校正（评审五） ----------

    // 媒体时间与真实时间的累积偏差（秒）。
    // 正 = 媒体时间比真实时间快（声卡实际采样率低于标称等）。
    // 长期播放时若一直累积，进度条会偏离真实时间，
    // 需要周期性调用 CorrectDrift() 渐进拉回。
    double GetWallDrift() const;

    // 渐进校正：把媒体时间轴向墙钟拉近。
    // 每次最多修正 maxNudge 秒（默认 5ms，小于一帧时长无感知），
    // 修正量为当前偏差的 20%；偏差小于 10ms 时不动作。
    // 返回本次实际校正量（秒）。
    double CorrectDrift(
        double maxNudge = 0.005);

private:

    // 当前墙钟（秒，steady_clock）
    static double WallNow();

private:

    mutable std::mutex mutex;   // 保护基准 / 已播时长

    double baseSeconds = 0.0;   // 时钟基准（Seek 目标时间）

    double playedSeconds = 0.0; // 已播输出时长（秒）

    std::atomic<double> speedFactor{ 1.0 };   // 播放速度

    // 8.4：墙钟基准（Reset 时记录，用于漂移检测）
    double baseWallSeconds = 0.0;

    bool wallInitialized = false;
};

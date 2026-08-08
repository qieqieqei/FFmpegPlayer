#pragma once

// ============================================================
// FrameScheduler - 帧渲染调度器（8.1）
//
// 把"视频帧 PTS"与"主时钟"对齐，计算渲染等待时间：
//
//   delay = videoPts - masterTime
//
//   delay > 0 : 视频超前，等待 delay 秒再渲染
//   delay < 0 : 视频落后，落后超过阈值交给 DropController 丢帧
//
// 职责：
//   1. 计算单帧延迟（有音频：对音频主时钟；无音频：对视频时钟）
//   2. 延迟钳制：异常跳变（Seek / 刚启动 / 断流恢复）不产生
//      超长等待或误丢帧
//   3. 分片等待：大延迟拆成小块 sleep，保持事件循环响应
//
// 纯计算组件，不持有线程/时钟；时钟值由调用方（Player）传入。
// ============================================================

class FrameScheduler
{
public:

    FrameScheduler();

    // 计算渲染延迟（秒）
    // videoPts  : 待渲染帧的 PTS
    // masterTime: 主时钟当前时间（音频时钟或视频时钟）
    // 返回 >0 等待 / <0 落后 / 0 立即渲染
    double ComputeDelay(
        double videoPts,
        double masterTime) const;

    // 延迟钳制：超出 [-maxBehind, maxAhead] 视为异常
    // （Seek 跳变 / 启动瞬间 / 断流重连后时间轴错位）
    // 返回钳制后的延迟，并可通过 isAbnormal 告知调用方
    double ClampDelay(
        double delay,
        bool* isAbnormal = nullptr) const;

    // 无音频模式：按帧率匀速播放时的帧间隔（秒）
    // = frameDuration / speed（speed <= 0 时按 1.0）
    double ComputeFrameInterval(
        double frameDuration,
        double speed) const;

    // 把大延迟拆成小块（max 100ms），保持事件循环响应
    // delay: 剩余待等待秒数
    // 返回本次应 sleep 的毫秒数（0 = 无需再等）
    int NextWaitMs(
        double delay) const;

    // 阈值配置
    void SetMaxAheadSec(double sec);     // 超前钳制上限，默认 1.0
    void SetMaxBehindSec(double sec);    // 落后钳制上限，默认 1.0

private:

    double maxAheadSec = 1.0;    // 超前上限（秒）

    double maxBehindSec = 1.0;   // 落后上限（秒）

    double waitChunkMs = 100.0;  // 分片等待粒度（毫秒）
};

#pragma once

// ============================================================
// SeekController - Seek 系统（6.5）
//
// 流程：
//
//   渲染线程:  Request(seconds)
//                  |
//                  v
//   Demux线程:  Execute()
//                  |
//                  +-- 1. 打断三个队列，唤醒阻塞线程
//                  +-- 2. demuxer->Seek(seconds)
//                  +-- 3. 清空队列旧数据
//                  +-- 4. 恢复队列
//                  +-- 5. seekHandled = true（渲染线程丢旧帧）
//                  +-- 6. seekGeneration++（视频/音频线程 flush 解码器）
//
// 视频/音频解码线程通过 GetGeneration() 检测 Seek：
//   代数变化 -> avcodec_flush_buffers + 清理内部缓冲
// ============================================================

#include <atomic>

class Demuxer;
class PacketQueue;
class FrameQueue;

class SeekController
{
public:

    SeekController();

    // 绑定依赖（Player::Init 时调用一次）
    void Attach(
        Demuxer* demuxer,
        PacketQueue* videoPacketQueue,
        PacketQueue* audioPacketQueue,
        FrameQueue* videoFrameQueue);

    // 请求 Seek（渲染线程调用，Demux 线程执行）
    void Request(
        double seconds);

    // 是否还有未执行的 Seek 请求
    bool HasRequest() const;

    // 当前 Seek 目标（秒）
    double GetTarget() const;

    // 执行 Seek（必须在 Demux 线程中调用）
    void Execute();

    // Seek 是否已执行完毕（渲染线程据此丢弃旧帧）
    bool IsHandled() const;

    void ClearHandled();

    // Seek 代数：每次 Execute 递增
    // 解码线程检测到变化即 flush 自己的解码器
    int GetGeneration() const;

private:

    Demuxer* demuxer = nullptr;                 // 解复用器

    PacketQueue* videoPacketQueue = nullptr;    // 视频包队列

    PacketQueue* audioPacketQueue = nullptr;    // 音频包队列

    FrameQueue* videoFrameQueue = nullptr;      // 视频帧队列

    // Seek 请求标志 + 目标时间
    std::atomic<bool> seekRequested{ false };

    std::atomic<double> seekTarget{ 0.0 };

    // Seek 已完成标志
    std::atomic<bool> seekHandled{ false };

    // Seek 代数
    std::atomic<int> seekGeneration{ 0 };
};

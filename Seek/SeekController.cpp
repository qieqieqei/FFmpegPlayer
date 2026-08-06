#include "Seek/SeekController.h"

#include "Demuxer.h"
#include "Queue/PacketQueue.h"
#include "Queue/FrameQueue.h"

#include "Utils/ErrorHandler.h"
#include "Utils/Logger.h"

#include <iostream>
#include <algorithm>

SeekController::SeekController()
{
}

void SeekController::Attach(
    Demuxer* demuxer,
    PacketQueue* videoPacketQueue,
    PacketQueue* audioPacketQueue,
    FrameQueue* videoFrameQueue)
{
    this->demuxer = demuxer;

    this->videoPacketQueue = videoPacketQueue;

    this->audioPacketQueue = audioPacketQueue;

    this->videoFrameQueue = videoFrameQueue;
}

void SeekController::Request(
    double seconds)
{
    if (seconds < 0.0)
    {
        seconds = 0.0;
    }

    // 记录 Seek 目标（由 Demux 线程消费）
    seekTarget.store(seconds);

    seekRequested.store(true);
}

bool SeekController::HasRequest() const
{
    return seekRequested.load();
}

double SeekController::GetTarget() const
{
    return seekTarget.load();
}

void SeekController::Execute()
{
    // 消费本次请求（防止重复执行）
    seekRequested.store(false);

    if (!demuxer ||
        !videoPacketQueue ||
        !audioPacketQueue ||
        !videoFrameQueue)
    {
        // 未 Attach，直接清掉请求
        seekRequested.store(false);

        return;
    }

    double target =
        seekTarget.load();

    // 目标时间夹在 [0, 时长] 内
    target =
        std::max(
            0.0,
            std::min(
                demuxer->GetDuration(),
                target));

    seekTarget.store(target);

    Logger::Info()
        << "[SeekController] Seek -> "
        << target
        << " s"
        << std::endl;

    // ---------- 1. 打断队列，唤醒阻塞线程 ----------

    videoPacketQueue->Interrupt();

    audioPacketQueue->Interrupt();

    videoFrameQueue->Interrupt();

    // ---------- 2. 定位文件 ----------

    demuxer->Seek(target);

    // ---------- 3. 清空旧数据 ----------

    videoPacketQueue->Clear();

    audioPacketQueue->Clear();

    videoFrameQueue->Clear();

    // ---------- 4. 恢复队列 ----------

    videoPacketQueue->ResetInterrupt();

    audioPacketQueue->ResetInterrupt();

    videoFrameQueue->ResetInterrupt();

    // ---------- 5. 标记 Seek 完成 ----------

    seekHandled.store(true);

    // Seek 代数 +1
    // 视频线程：检测变化 -> flush 视频解码器
    // 音频线程：检测变化 -> flush 音频解码器 + 重置时钟/变速器
    seekGeneration.fetch_add(1);

    Logger::Info()
        << "[SeekController] Seek Done"
        << std::endl;
}

bool SeekController::IsHandled() const
{
    return seekHandled.load();
}

void SeekController::ClearHandled()
{
    seekHandled.store(false);
}

int SeekController::GetGeneration() const
{
    return seekGeneration.load();
}

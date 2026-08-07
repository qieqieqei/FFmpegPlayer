#include "Network/BufferController.h"

// ============================================================
// BufferController - 网络播放缓冲控制
// ============================================================

BufferController::BufferController()
{
}

void BufferController::SetLive(
    bool live)
{
    this->live = live;

    // 直播：低延迟优先，目标缓冲小
    // 点播：流畅优先，目标缓冲大
    if (live)
    {
        targetMs = 300;

        lowWaterMs = 150;

        highWaterMs = 400;
    }
    else
    {
        targetMs = 2000;

        lowWaterMs = 500;

        highWaterMs = 1500;
    }
}

bool BufferController::IsLive() const
{
    return live;
}

void BufferController::SetTargetBufferMs(
    int ms)
{
    if (ms > 0)
    {
        targetMs = ms;

        // 水位随目标自动缩放：低 = 50% 目标，高 = 130% 目标
        lowWaterMs =
            targetMs / 2;

        highWaterMs =
            static_cast<int>(targetMs * 1.3);
    }
}

void BufferController::SetWatermarks(
    int lowMs,
    int highMs)
{
    if (lowMs > 0)
    {
        lowWaterMs = lowMs;
    }

    if (highMs > lowMs)
    {
        highWaterMs = highMs;
    }
}

void BufferController::Update(
    double bufferedMs)
{
    bufferedMs =
        bufferedMs < 0.0 ?
        0.0 :
        bufferedMs;

    this->bufferedMs =
        bufferedMs;

    // 状态跳变：进入"缓冲中"时产生一次性事件
    bool buffering =
        bufferedMs < lowWaterMs;

    if (buffering &&
        !wasBuffering)
    {
        bufferingEvent = true;
    }

    wasBuffering = buffering;
}

bool BufferController::NeedBuffer() const
{
    // 低于低水位：缓冲不足，需要缓冲
    return bufferedMs < lowWaterMs;
}

bool BufferController::IsEnough() const
{
    // 高于高水位：缓冲充足
    return bufferedMs > highWaterMs;
}

double BufferController::GetBufferedMs() const
{
    return bufferedMs;
}

int BufferController::GetTargetMs() const
{
    return targetMs;
}

bool BufferController::ConsumeBufferingEvent()
{
    bool evt = bufferingEvent;

    bufferingEvent = false;

    return evt;
}

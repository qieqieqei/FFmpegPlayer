#include "Queue/FrameQueue.h"

#include <chrono>

FrameQueue::FrameQueue()
    : interrupted(false)
{
}

FrameQueue::~FrameQueue()
{
    Clear();
}

bool FrameQueue::Push(
    FramePtr&& frame,
    int maxSize)
{
    if (!frame)
    {
        return false;
    }

    std::unique_lock<std::mutex> lock(mutex);

    // 谓词等待：队列不满或被打断即返回（8.4：替代 wait_for(10ms) 轮询）
    cv.wait(
        lock,
        [this, maxSize]
        {
            return
                interrupted.load() ||
                static_cast<int>(queue.size()) < maxSize;
        });

    if (interrupted.load())
    {
        return false;
    }

    queue.push(std::move(frame));

    cv.notify_all();

    return true;
}

FramePtr FrameQueue::Pop(
    int timeoutMs)
{
    std::unique_lock<std::mutex> lock(mutex);

    if (timeoutMs <= 0)
    {
        // 不等待：仅尝试一次
        if (queue.empty())
        {
            return FramePtr();
        }
    }
    else
    {
        // 谓词等待：有帧或被打断立即返回，无需轮询
        cv.wait_for(
            lock,
            std::chrono::milliseconds(timeoutMs),
            [this]
            {
                return
                    interrupted.load() ||
                    !queue.empty();
            });
    }

    if (queue.empty())
    {
        return FramePtr();
    }

    FramePtr frame =
        std::move(queue.front());

    queue.pop();

    cv.notify_all();

    return frame;
}

void FrameQueue::Clear()
{
    std::lock_guard<std::mutex> lock(mutex);

    // FramePtr 析构自动 av_frame_free
    while (!queue.empty())
    {
        queue.pop();
    }

    cv.notify_all();
}

int FrameQueue::Size() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return static_cast<int>(queue.size());
}

void FrameQueue::Interrupt()
{
    interrupted.store(true);

    cv.notify_all();
}

void FrameQueue::ResetInterrupt()
{
    interrupted.store(false);

    cv.notify_all();
}

bool FrameQueue::IsInterrupted() const
{
    return interrupted.load();
}

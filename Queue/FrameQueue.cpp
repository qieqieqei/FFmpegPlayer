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
    AVFrame* frame,
    int maxSize)
{
    if (!frame)
    {
        return false;
    }

    std::unique_lock<std::mutex> lock(mutex);

    while (
        !interrupted.load() &&
        static_cast<int>(queue.size()) >= maxSize)
    {
        cv.wait_for(
            lock,
            std::chrono::milliseconds(10));
    }

    if (interrupted.load())
    {
        return false;
    }

    queue.push(frame);

    cv.notify_all();

    return true;
}

AVFrame* FrameQueue::Pop(
    int timeoutMs)
{
    std::unique_lock<std::mutex> lock(mutex);

    if (queue.empty())
    {
        if (timeoutMs <= 0)
        {
            return nullptr;
        }

        cv.wait_for(
            lock,
            std::chrono::milliseconds(timeoutMs));
    }

    if (queue.empty())
    {
        return nullptr;
    }

    AVFrame* frame = queue.front();

    queue.pop();

    return frame;
}

void FrameQueue::Clear()
{
    std::lock_guard<std::mutex> lock(mutex);

    while (!queue.empty())
    {
        AVFrame* frame = queue.front();

        queue.pop();

        av_frame_free(&frame);
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

#include "Queue/PacketQueue.h"

#include <chrono>

PacketQueue::PacketQueue()
    : interrupted(false)
{
}

PacketQueue::~PacketQueue()
{
    Clear();
}

bool PacketQueue::Push(
    AVPacket* pkt,
    int maxSize)
{
    if (!pkt)
    {
        return false;
    }

    std::unique_lock<std::mutex> lock(mutex);

    // 队列满了就等待（背压：让 Demux 线程慢下来）
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
        return false;   // 被打断，调用者自行释放 pkt
    }

    queue.push(pkt);

    cv.notify_all();

    return true;
}

AVPacket* PacketQueue::Pop(
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

    AVPacket* pkt = queue.front();

    queue.pop();

    return pkt;
}

void PacketQueue::Clear()
{
    std::lock_guard<std::mutex> lock(mutex);

    while (!queue.empty())
    {
        AVPacket* pkt = queue.front();

        queue.pop();

        av_packet_free(&pkt);
    }

    cv.notify_all();
}

int PacketQueue::Size() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return static_cast<int>(queue.size());
}

void PacketQueue::Interrupt()
{
    interrupted.store(true);

    cv.notify_all();
}

void PacketQueue::ResetInterrupt()
{
    interrupted.store(false);

    cv.notify_all();
}

bool PacketQueue::IsInterrupted() const
{
    return interrupted.load();
}

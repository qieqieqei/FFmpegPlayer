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
    PacketPtr&& pkt,
    int maxSize)
{
    if (!pkt)
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
        return false;   // 被打断，pkt 仍归调用者（RAII 自动释放）
    }

    queue.push(std::move(pkt));

    cv.notify_all();

    return true;
}

PacketPtr PacketQueue::Pop(
    int timeoutMs)
{
    std::unique_lock<std::mutex> lock(mutex);

    if (timeoutMs <= 0)
    {
        // 不等待：仅尝试一次
        if (queue.empty())
        {
            return PacketPtr();
        }
    }
    else
    {
        // 谓词等待：有包或被打断立即返回，无需轮询
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
        return PacketPtr();
    }

    PacketPtr pkt =
        std::move(queue.front());

    queue.pop();

    return pkt;
}

void PacketQueue::Clear()
{
    std::lock_guard<std::mutex> lock(mutex);

    // PacketPtr 析构自动 av_packet_free
    while (!queue.empty())
    {
        queue.pop();
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

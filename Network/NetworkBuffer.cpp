#include "Network/NetworkBuffer.h"

// ============================================================
// NetworkBuffer - 网络缓冲队列
// ============================================================

NetworkBuffer::NetworkBuffer()
{
}

NetworkBuffer::~NetworkBuffer()
{
    Clear();
}

void NetworkBuffer::SetMaxSize(
    int maxSize)
{
    this->maxSize.store(
        maxSize > 0 ? maxSize : 1);
}

int NetworkBuffer::GetMaxSize() const
{
    return maxSize.load();
}

bool NetworkBuffer::Push(
    AVPacket* pkt)
{
    if (!pkt)
    {
        return false;
    }

    std::unique_lock<std::mutex> lock(mutex);

    if (interrupted.load())
    {
        return false;
    }

    // 队列满：丢弃最旧包，保持低延迟（直播场景不阻塞）
    while (static_cast<int>(queue.size()) >=
        maxSize.load())
    {
        AVPacket* oldest =
            queue.front();

        queue.pop_front();

        av_packet_free(&oldest);

        dropped.fetch_add(1);
    }

    queue.push_back(pkt);

    cv.notify_one();

    return true;
}

AVPacket* NetworkBuffer::Pop(
    int timeoutMs)
{
    std::unique_lock<std::mutex> lock(mutex);

    if (interrupted.load())
    {
        return nullptr;
    }

    if (queue.empty())
    {
        if (timeoutMs <= 0)
        {
            return nullptr;
        }

        cv.wait_for(
            lock,
            std::chrono::milliseconds(timeoutMs),
            [this]
            {
                return
                    !queue.empty() ||
                    interrupted.load();
            });

        if (queue.empty() ||
            interrupted.load())
        {
            return nullptr;
        }
    }

    AVPacket* pkt =
        queue.front();

    queue.pop_front();

    return pkt;
}

void NetworkBuffer::Clear()
{
    std::unique_lock<std::mutex> lock(mutex);

    while (!queue.empty())
    {
        AVPacket* pkt =
            queue.front();

        queue.pop_front();

        av_packet_free(&pkt);
    }
}

int NetworkBuffer::Size() const
{
    std::unique_lock<std::mutex> lock(mutex);

    return static_cast<int>(queue.size());
}

void NetworkBuffer::Interrupt()
{
    interrupted.store(true);

    cv.notify_all();
}

void NetworkBuffer::ResetInterrupt()
{
    interrupted.store(false);
}

bool NetworkBuffer::IsInterrupted() const
{
    return interrupted.load();
}

int64_t NetworkBuffer::GetDroppedCount() const
{
    return dropped.load();
}

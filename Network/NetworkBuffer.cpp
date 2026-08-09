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
    PacketPtr&& pkt)
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

    // 队列满：GOP 感知丢包（8.4）——保持解码链完整
    //
    // 背景：盲目丢最旧包会撕裂 GOP（丢掉 P/B 帧的参考链），
    //       解码器花屏直到下一个关键帧。
    // 策略：
    //   1) 队头非关键帧：从队头丢到【第一个关键帧之前】，
    //      保留完整 GOP 起点，后续帧仍可正常解码（不花屏）；
    //   2) 队头就是关键帧（整个队列同属一个 GOP）：
    //      整段丢弃，腾出空间，等下一个关键帧重建画面。
    // 效果：丢包粒度 = GOP 段，代价可控，画面始终能从关键帧恢复。
    while (static_cast<int>(queue.size()) >=
        maxSize.load())
    {
        if (queue.front()->flags &
            AV_PKT_FLAG_KEY)
        {
            // 队头是关键帧：整段丢弃（当前 GOP 无法部分丢弃）
            while (!queue.empty())
            {
                queue.pop_front();

                dropped.fetch_add(1);
            }

            break;
        }

        // 队头非关键帧：丢到第一个关键帧之前
        while (!queue.empty() &&
            !(queue.front()->flags &
                AV_PKT_FLAG_KEY))
        {
            queue.pop_front();

            dropped.fetch_add(1);
        }

        // 此时队头是关键帧（或队列空）；若仍满（关键帧密集流），
        // 下一轮循环走队头清空分支
    }

    queue.push_back(std::move(pkt));

    cv.notify_one();

    return true;
}

PacketPtr NetworkBuffer::Pop(
    int timeoutMs)
{
    std::unique_lock<std::mutex> lock(mutex);

    if (interrupted.load())
    {
        return PacketPtr();
    }

    if (queue.empty())
    {
        if (timeoutMs <= 0)
        {
            return PacketPtr();
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
            return PacketPtr();
        }
    }

    PacketPtr pkt =
        std::move(queue.front());

    queue.pop_front();

    return pkt;
}

void NetworkBuffer::Clear()
{
    std::unique_lock<std::mutex> lock(mutex);

    // PacketPtr 析构自动 av_packet_free
    while (!queue.empty())
    {
        queue.pop_front();
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

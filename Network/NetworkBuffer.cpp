#include "Network/NetworkBuffer.h"

extern "C" {
#include <libavutil/mathematics.h>
}

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

    // 队列满（包数 / 内存上限）：GOP 感知丢包（8.4）——保持解码链完整
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
            maxSize.load() ||
        (maxMemoryBytes > 0 &&
            queueBytes + pkt->size > maxMemoryBytes))
    {
        if (queue.front()->flags &
            AV_PKT_FLAG_KEY)
        {
            // 队头是关键帧：整段丢弃（当前 GOP 无法部分丢弃）
            while (!queue.empty())
            {
                queueBytes -= queue.front()->size;

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
            queueBytes -= queue.front()->size;

            queue.pop_front();

            dropped.fetch_add(1);
        }

        // 此时队头是关键帧（或队列空）；若仍满（关键帧密集流），
        // 下一轮循环走队头清空分支
    }

    queue.push_back(std::move(pkt));

    queueBytes += queue.back()->size;

    // 8.5：直播时长上限——积压超过阈值丢旧包追最新
    TrimLiveLocked();

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

    queueBytes -= pkt->size;

    return pkt;
}

void NetworkBuffer::Clear()
{
    std::unique_lock<std::mutex> lock(mutex);

    // PacketPtr 析构自动 av_packet_free
    while (!queue.empty())
    {
        queueBytes -= queue.front()->size;

        queue.pop_front();
    }

    queueBytes = 0;
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

void NetworkBuffer::SetLiveDurationMs(
    int maxDurationMs,
    int64_t timeBaseNum,
    int64_t timeBaseDen)
{
    std::lock_guard<std::mutex> lock(mutex);

    liveDurationMs =
        maxDurationMs > 0 ? maxDurationMs : 0;

    if (timeBaseDen > 0)
    {
        liveTimeBase.num =
            static_cast<int>(timeBaseNum);

        liveTimeBase.den =
            static_cast<int>(timeBaseDen);
    }
}

int NetworkBuffer::GetLiveDurationMs() const
{
    return liveDurationMs;
}

void NetworkBuffer::SetDurationTrimEnabled(
    bool enabled)
{
    std::lock_guard<std::mutex> lock(mutex);

    durationTrimEnabled = enabled;
}

bool NetworkBuffer::GetDurationTrimEnabled() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return durationTrimEnabled;
}

void NetworkBuffer::SetMaxMemoryBytes(
    size_t maxBytes)
{
    std::lock_guard<std::mutex> lock(mutex);

    maxMemoryBytes = maxBytes;
}

size_t NetworkBuffer::GetMaxMemoryBytes() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return maxMemoryBytes;
}

size_t NetworkBuffer::GetQueueBytes() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return queueBytes;
}

int64_t NetworkBuffer::GetDurationMs() const
{
    std::lock_guard<std::mutex> lock(mutex);

    if (queue.size() < 2)
    {
        return 0;
    }

    int64_t frontTs =
        queue.front()->pts;

    if (frontTs == AV_NOPTS_VALUE)
    {
        frontTs = queue.front()->dts;
    }

    int64_t backTs =
        queue.back()->pts;

    if (backTs == AV_NOPTS_VALUE)
    {
        backTs = queue.back()->dts;
    }

    if (frontTs == AV_NOPTS_VALUE ||
        backTs == AV_NOPTS_VALUE)
    {
        return 0;
    }

    return av_rescale_q(
        backTs - frontTs,
        liveTimeBase,
        AVRational{ 1, 1000 });
}

int64_t NetworkBuffer::GetDroppedCount() const
{
    return dropped.load();
}

double NetworkBuffer::GetFrontPts() const
{
    std::lock_guard<std::mutex> lock(mutex);

    if (queue.empty())
    {
        return -1.0;
    }

    int64_t frontTs =
        queue.front()->pts;

    if (frontTs == AV_NOPTS_VALUE)
    {
        frontTs = queue.front()->dts;
    }

    if (frontTs == AV_NOPTS_VALUE)
    {
        return -1.0;
    }

    return static_cast<double>(
               av_rescale_q(
                   frontTs,
                   liveTimeBase,
                   AVRational{ 1, 1000000 })) /
        1000000.0;
}

double NetworkBuffer::GetBackPts() const
{
    std::lock_guard<std::mutex> lock(mutex);

    if (queue.empty())
    {
        return -1.0;
    }

    int64_t backTs =
        queue.back()->pts;

    if (backTs == AV_NOPTS_VALUE)
    {
        backTs = queue.back()->dts;
    }

    if (backTs == AV_NOPTS_VALUE)
    {
        return -1.0;
    }

    return static_cast<double>(
               av_rescale_q(
                   backTs,
                   liveTimeBase,
                   AVRational{ 1, 1000000 })) /
        1000000.0;
}

void NetworkBuffer::TrimLiveLocked()
{
    // 时长修剪关闭（缓冲状态机）或未开启：不修剪
    if (!durationTrimEnabled ||
        liveDurationMs <= 0)
    {
        return;
    }

    // 8.5：直播追最新——队列积压超过阈值丢旧包。
    // 与 8.4 包数上限共用同一套 GOP 感知策略：
    //   1) 队头非关键帧：丢到第一个关键帧之前（保留 GOP 起点）
    //   2) 队头是关键帧：整段丢弃，等下一个关键帧重建
    while (queue.size() >= 2)
    {
        int64_t frontTs =
            queue.front()->pts;

        if (frontTs == AV_NOPTS_VALUE)
        {
            frontTs = queue.front()->dts;
        }

        int64_t backTs =
            queue.back()->pts;

        if (backTs == AV_NOPTS_VALUE)
        {
            backTs = queue.back()->dts;
        }

        if (frontTs == AV_NOPTS_VALUE ||
            backTs == AV_NOPTS_VALUE)
        {
            return;
        }

        int64_t durationMs =
            av_rescale_q(
                backTs - frontTs,
                liveTimeBase,
                AVRational{ 1, 1000 });

        if (durationMs <= liveDurationMs)
        {
            return;
        }

        if (queue.front()->flags &
            AV_PKT_FLAG_KEY)
        {
            while (!queue.empty())
            {
                queueBytes -= queue.front()->size;

                queue.pop_front();

                dropped.fetch_add(1);
            }

            break;
        }

        while (!queue.empty() &&
            !(queue.front()->flags &
                AV_PKT_FLAG_KEY))
        {
            queueBytes -= queue.front()->size;

            queue.pop_front();

            dropped.fetch_add(1);
        }
    }
}

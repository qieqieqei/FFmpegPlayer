#include "Queue/PacketQueue.h"

#include <chrono>

extern "C" {
#include <libavutil/mathematics.h>
}

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

    if (liveMode)
    {
        // ---------- 直播模式（8.5）：不阻塞，追最新画面 ----------
        // 直播的队列不能像文件那样背压——网络数据是匀速到达的，
        // 阻塞只会让 Demux 线程卡住、延迟无限累积。
        // 直接入队，然后按队列时长丢旧包（GOP 感知）。

        if (interrupted.load())
        {
            return false;
        }

        queue.push(std::move(pkt));

        TrimLiveLocked();

        cv.notify_all();

        return true;
    }

    // 点播：谓词等待，队列不满或被打断即返回（8.4）
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

void PacketQueue::SetLiveMode(
    bool enable,
    int timeBaseNum,
    int timeBaseDen,
    int maxDurationMs)
{
    std::lock_guard<std::mutex> lock(mutex);

    liveMode = enable;

    // 非法分母防御（避免除零）
    if (timeBaseDen > 0)
    {
        liveTimeBase.num = timeBaseNum;

        liveTimeBase.den = timeBaseDen;
    }

    if (maxDurationMs > 0)
    {
        liveMaxDurationMs = maxDurationMs;
    }

    droppedCount = 0;

    // 切换模式时清掉积压（直播切点播 / 点播切直播）
    while (!queue.empty())
    {
        queue.pop();
    }

    cv.notify_all();
}

bool PacketQueue::IsLiveMode() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return liveMode;
}

int64_t PacketQueue::GetDroppedCount() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return droppedCount;
}

void PacketQueue::TrimLiveLocked()
{
    // 至少两个包才有"时长"可言
    if (queue.size() < 2)
    {
        return;
    }

    // 队首 / 队尾时间戳（pts 优先，缺省用 dts；都没有则不判断）
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

    // 循环修剪：丢一次后队头变化（可能仍超时），继续检查
    while (queue.size() >= 2)
    {
        // 队首 / 队尾时间戳（每轮重新取，因为队头在变）
        frontTs = queue.front()->pts;

        if (frontTs == AV_NOPTS_VALUE)
        {
            frontTs = queue.front()->dts;
        }

        backTs = queue.back()->pts;

        if (backTs == AV_NOPTS_VALUE)
        {
            backTs = queue.back()->dts;
        }

        if (frontTs == AV_NOPTS_VALUE ||
            backTs == AV_NOPTS_VALUE)
        {
            return;
        }

        // 队列时长（毫秒）：(backTs - frontTs) 换算到毫秒
        int64_t durationMs =
            av_rescale_q(
                backTs - frontTs,
                liveTimeBase,
                AVRational{ 1, 1000 });

        if (durationMs <= liveMaxDurationMs)
        {
            // 积压未超阈值：不丢
            return;
        }

        // 超阈值：丢旧包（GOP 感知，8.4 教训）
        //   1) 队头是关键帧：整段清空，等下一个关键帧重建画面
        //   2) 队头非关键帧：丢到第一个关键帧之前，
        //      保留完整 GOP 起点，后续帧仍可正常解码（不花屏）
        if (queue.front()->flags &
            AV_PKT_FLAG_KEY)
        {
            while (!queue.empty())
            {
                queue.pop();

                droppedCount++;
            }

            break;
        }

        while (!queue.empty() &&
            !(queue.front()->flags &
                AV_PKT_FLAG_KEY))
        {
            queue.pop();

            droppedCount++;
        }
    }
}

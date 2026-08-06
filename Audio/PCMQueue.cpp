#include "Audio/PCMQueue.h"

#include <cstring>
#include <chrono>
#include <algorithm>

PCMQueue::PCMQueue()
{
}

PCMQueue::~PCMQueue()
{
    Clear();
}

void PCMQueue::SetMaxBytes(
    size_t maxBytes)
{
    maxQueueBytes = maxBytes;
}

void PCMQueue::Push(
    const uint8_t* data,
    int size,
    const std::atomic<bool>* abort)
{
    if (!data || size <= 0)
    {
        return;
    }

    std::unique_lock<std::mutex> lock(mutex);

    // 背压：缓冲满了就等待
    // 等待期间检查 interrupted 和 abort（Seek/退出时立即放弃）
    while (
        !interrupted.load() &&
        !(abort && abort->load()) &&
        queueBytes + static_cast<size_t>(size) >
            maxQueueBytes)
    {
        cv.wait_for(
            lock,
            std::chrono::milliseconds(10));
    }

    if (interrupted.load() ||
        (abort && abort->load()))
    {
        return;   // Seek/退出中，丢弃本次数据
    }

    chunks.emplace_back(
        data,
        data + size);

    queueBytes +=
        static_cast<size_t>(size);

    cv.notify_all();
}

int PCMQueue::Pop(
    uint8_t* stream,
    int len)
{
    if (!stream || len <= 0)
    {
        return 0;
    }

    std::lock_guard<std::mutex> lock(mutex);

    int written = 0;

    while (written < len &&
        !chunks.empty())
    {
        std::vector<uint8_t>& chunk =
            chunks.front();

        int copy =
            static_cast<int>(
                std::min<size_t>(
                    chunk.size(),
                    len - written));

        std::memcpy(
            stream + written,
            chunk.data(),
            copy);

        written += copy;

        if (copy < static_cast<int>(chunk.size()))
        {
            // 块没取完，留下剩余部分
            chunk.erase(
                chunk.begin(),
                chunk.begin() + copy);

            queueBytes -=
                static_cast<size_t>(copy);
        }
        else
        {
            queueBytes -=
                chunk.size();

            chunks.pop_front();
        }
    }

    return written;
}

int PCMQueue::GetQueuedBytes() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return static_cast<int>(queueBytes);
}

void PCMQueue::Clear()
{
    std::lock_guard<std::mutex> lock(mutex);

    chunks.clear();

    queueBytes = 0;

    cv.notify_all();
}

void PCMQueue::Interrupt()
{
    interrupted.store(true);

    cv.notify_all();
}

void PCMQueue::ResetInterrupt()
{
    interrupted.store(false);
}

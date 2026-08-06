#pragma once

// ============================================================
// PCMQueue - PCM 数据队列（6.3）
//
// 数据流：
//
//   AudioDecoder ---> Resampler ---> SpeedController ---> Push()
//                                                          |
//                                                          v
//                                                    PCMQueue（带背压）
//                                                          |
//                                                          v
//                                                 SDL AudioCallback
//
// 职责：
//   - 缓存重采样/变速后的 PCM（S16）
//   - 背压：队列达到最大缓冲时 Push 阻塞，
//     防止音频解码跑在播放前面太远
//   - Interrupt：Seek / 退出时唤醒阻塞中的 Push
//   - abort：可选的原子标志，置位时 Push 立即放弃（Seek 死锁防护）
// ============================================================

#include <deque>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstdint>

class PCMQueue
{
public:

    PCMQueue();

    ~PCMQueue();

    // 设置最大缓冲字节数（约 2 秒音频）
    void SetMaxBytes(
        size_t maxBytes);

    // 推入 PCM 数据（拷贝进队列）
    // abort: 等待期间若 *abort == true 则放弃推送，立即返回
    void Push(
        const uint8_t* data,
        int size,
        const std::atomic<bool>* abort = nullptr);

    // 从队列取数据填充 stream（SDL 回调用）
    // 返回实际填充的字节数（不足部分由调用方补静音）
    int Pop(
        uint8_t* stream,
        int len);

    // 当前缓冲字节数
    int GetQueuedBytes() const;

    // 清空队列
    void Clear();

    // 打断阻塞中的 Push（Seek / 退出）
    void Interrupt();

    void ResetInterrupt();

private:

    // PCM 数据队列（按块存储，避免逐字节拷贝）
    std::deque<std::vector<uint8_t>> chunks;

    // 队列总字节数
    size_t queueBytes = 0;

    // 最大缓冲（2 秒音频）
    size_t maxQueueBytes = 48000 * 2 * 2 * 2;

    // 保护队列
    mutable std::mutex mutex;

    // 唤醒阻塞中的 Push
    std::condition_variable cv;

    // 打断标志（Seek / 退出）
    std::atomic<bool> interrupted{ false };
};

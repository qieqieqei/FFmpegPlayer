#pragma once

// ============================================================
// PacketQueue - 线程安全 AVPacket 队列（5.3）
//
// 架构升级后的中间缓存：
//
//   Demux Thread                    Decoder Thread
//   av_read_frame()                 packetQueue.Pop()
//        |                                |
//        v                                v
//   packetQueue.Push()   ------>    avcodec_send_packet()
//
// 特点：
//   - 线程安全（mutex + condition_variable）
//   - 支持最大长度限制，满了 Push 阻塞（背压，防止内存暴涨）
//   - 支持 Interrupt 打断阻塞（Seek / 退出时唤醒等待线程）
//   - Push 成功即接管 AVPacket 所有权，由队列负责释放
// ============================================================

#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>

extern "C" {
#include <libavcodec/avcodec.h>
}

class PacketQueue
{
public:

    PacketQueue();

    ~PacketQueue();

    // 推入一个 Packet
    // maxSize: 队列最大 Packet 数量，达到后阻塞等待
    // 返回 true  = 入队成功（队列接管 pkt 所有权）
    // 返回 false = 被 Interrupt 打断（调用者需要自己 av_packet_free）
    bool Push(
        AVPacket* pkt,
        int maxSize);

    // 取出一个 Packet（调用者负责释放）
    // timeoutMs: 等待毫秒数，0 = 不等待
    // 返回 nullptr = 超时或被打断
    AVPacket* Pop(
        int timeoutMs);

    // 清空队列（释放所有 Packet）
    void Clear();

    // 当前队列大小
    int Size() const;

    // 打断所有阻塞的 Push/Pop（Seek / 退出时调用）
    void Interrupt();

    // 取消打断状态
    void ResetInterrupt();

    bool IsInterrupted() const;

private:

    std::queue<AVPacket*> queue;    // Packet 队列

    mutable std::mutex mutex;       // 保护队列

    std::condition_variable cv;     // 唤醒等待线程

    std::atomic<bool> interrupted;  // 打断标志
};

#pragma once

// ============================================================
// PacketQueue - 线程安全 AVPacket 队列（5.3 / 8.4 所有权改造）
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
//   - 8.4：元素类型改 PacketPtr（unique 所有权），
//     Push 移动语义交接所有权，Pop 返回所有权，
//     队列 / 生产者 / 消费者之间不再有裸指针共享
//   - 8.4：等待改谓词等待（cv.wait / wait_for(lock, pred)），
//     取代 wait_for(10ms) 轮询，条件满足立即唤醒
// ============================================================

#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>

#include "Utils/FFmpegPtr.h"

class PacketQueue
{
public:

    PacketQueue();

    ~PacketQueue();

    // 推入一个 Packet（移动语义，队列接管所有权）
    // maxSize: 队列最大 Packet 数量，达到后阻塞等待（谓词等待）
    // 返回 true  = 入队成功（pkt 所有权已移交）
    // 返回 false = 被 Interrupt 打断（pkt 仍归调用者，析构自动释放）
    bool Push(
        PacketPtr&& pkt,
        int maxSize);

    // 取出一个 Packet（返回所有权，调用者无需释放）
    // timeoutMs: 等待毫秒数，0 = 不等待
    // 返回空 = 超时或被打断
    PacketPtr Pop(
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

    std::queue<PacketPtr> queue;    // Packet 队列（unique 所有权）

    mutable std::mutex mutex;       // 保护队列

    std::condition_variable cv;     // 唤醒等待线程

    std::atomic<bool> interrupted;  // 打断标志
};

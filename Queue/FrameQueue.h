#pragma once

// ============================================================
// FrameQueue - 线程安全 AVFrame 队列（5.3）
//
//   Decoder Thread                  Render Thread
//   avcodec_receive_frame()         frameQueue.Pop()
//        |                                |
//        v                                v
//   frameQueue.Push()   ------>     SDL Render
//
// 特点：
//   - 线程安全（mutex + condition_variable）
//   - 最大长度限制，满了 Push 阻塞（防止解码跑在渲染前面太远）
//   - 支持 Interrupt 打断阻塞（Seek / 退出时唤醒）
//   - Push 成功即接管 AVFrame 所有权，由队列负责释放
// ============================================================

#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>

extern "C" {
#include <libavutil/frame.h>
}

class FrameQueue
{
public:

    FrameQueue();

    ~FrameQueue();

    // 推入一个 Frame（队列接管所有权）
    // 返回 false = 被打断，调用者自行释放
    bool Push(
        AVFrame* frame,
        int maxSize);

    // 取出一个 Frame（调用者负责释放）
    // timeoutMs: 等待毫秒数，0 = 不等待
    AVFrame* Pop(
        int timeoutMs);

    // 清空队列（释放所有 Frame）
    void Clear();

    // 当前队列大小
    int Size() const;

    // 打断所有阻塞调用
    void Interrupt();

    void ResetInterrupt();

    // 是否处于打断状态（Seek / 退出）
    bool IsInterrupted() const;

private:

    std::queue<AVFrame*> queue;     // Frame 队列

    mutable std::mutex mutex;

    std::condition_variable cv;

    std::atomic<bool> interrupted;
};

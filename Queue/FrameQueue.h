#pragma once

// ============================================================
// FrameQueue - 线程安全 AVFrame 队列（5.3 / 8.4 所有权改造）
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
//   - 8.4：元素类型改 FramePtr（unique 所有权），
//     Push 移动语义交接所有权，Pop 返回所有权
//   - 8.4：等待改谓词等待（cv.wait / wait_for(lock, pred)）
// ============================================================

#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>

#include "Utils/FFmpegPtr.h"

class FrameQueue
{
public:

    FrameQueue();

    ~FrameQueue();

    // 推入一个 Frame（移动语义，队列接管所有权）
    // 返回 false = 被打断（frame 仍归调用者，析构自动释放）
    bool Push(
        FramePtr&& frame,
        int maxSize);

    // 取出一个 Frame（返回所有权，调用者无需释放）
    // timeoutMs: 等待毫秒数，0 = 不等待
    FramePtr Pop(
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

    std::queue<FramePtr> queue;     // Frame 队列（unique 所有权）

    mutable std::mutex mutex;

    std::condition_variable cv;

    std::atomic<bool> interrupted;
};

#pragma once

// ============================================================
// NetworkBuffer - 网络缓冲队列（7.2 / 7.3 / 8.4 所有权改造）
//
// 作用：解决网络抖动。
//
//   与本地 PacketQueue 的区别：
//     本地：读取速度 > 播放速度，队列满时 Push 阻塞（背压）
//     网络：网络速度随时变化，直播场景不能无限积压延迟，
//           队列满时【GOP 感知丢包】（8.4）：丢到关键帧边界，
//           保持解码链完整，杜绝花屏
//
// 接口（与 PacketQueue 对齐，方便互换）：
//   Push(pkt)        入队（移动语义）；满时丢到关键帧边界（记入 dropped）
//   Pop(timeoutMs)   出队；超时 / 被打断返回空
//   Interrupt()      打断阻塞（Seek / 退出时唤醒）
//
// 8.4：元素类型改 PacketPtr（unique 所有权），
//      丢弃/出队均自动释放，杜绝裸指针共享。
// ============================================================

#include <deque>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include "Utils/FFmpegPtr.h"

class NetworkBuffer
{
public:

    NetworkBuffer();

    ~NetworkBuffer();

    // 设置最大包数（默认 600）
    void SetMaxSize(
        int maxSize);

    int GetMaxSize() const;

    // ---------- 直播时长上限（8.5） ----------

    // 设置直播队列时长上限（毫秒）。队列积压超过该值就丢旧包
    // 追最新画面（GOP 感知，与包数上限叠加生效）。
    //   maxDurationMs <= 0 : 关闭（默认，仅按包数丢）
    //   timeBaseNum/Den    : 包 time_base（pts 换算秒用，通常取视频流 time_base）
    void SetLiveDurationMs(
        int maxDurationMs,
        int64_t timeBaseNum,
        int64_t timeBaseDen);

    int GetLiveDurationMs() const;

    // 直播时长修剪开关（缓冲状态机用）：
    // PREBUFFERING / REBUFFERING 期间应关闭——
    // 防止"一边积压一边丢旧"（v2 设计：积压本身就是要攒的水位）。
    // 恢复 PLAYING 后重新开启（默认开启）。
    void SetDurationTrimEnabled(
        bool enabled);

    bool GetDurationTrimEnabled() const;

    // 设置队列内存上限（字节）。0 = 不限制（默认）。
    // 与包数上限 / 时长上限叠加生效（v2：三重上限）。
    void SetMaxMemoryBytes(
        size_t maxBytes);

    size_t GetMaxMemoryBytes() const;

    // 当前队列内存占用（字节，packet->size 累计）
    size_t GetQueueBytes() const;

    // 当前队列时长（毫秒，按队首/队尾 pts 估算；缺 pts 返回 0）
    int64_t GetDurationMs() const;

    // v2: pts of the oldest buffered packet (seconds, liveTimeBase),
    // -1.0 when empty or no valid timestamp. Used to re-anchor the audio
    // clock when buffering ends (resume from the buffer point instead of
    // waiting for the frozen clock to catch up).
    double GetFrontPts() const;

    // v2: pts of the newest buffered packet (seconds, liveTimeBase),
    // -1.0 when empty or no valid timestamp. Together with GetFrontPts it
    // yields the live backlog span; against the audio clock it gives the
    // audio-side buffer depth (how far audio lags the push head).
    double GetBackPts() const;

    // 入队一个 Packet（移动语义，队列接管所有权）。
    // 队列满时：丢弃最旧的包（droppedCount++），新包入队。
    // 返回 true  = 入队成功
    // 返回 false = 被打断（pkt 仍归调用者，析构自动释放）
    bool Push(
        PacketPtr&& pkt);

    // 取出一个 Packet（返回所有权，调用者无需释放）
    // timeoutMs: 等待毫秒数，0 = 不等待
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

    // 因队列满被丢弃的包数（统计：丢包率分子）
    int64_t GetDroppedCount() const;

private:

    std::deque<PacketPtr> queue;    // Packet 队列（unique 所有权）

    mutable std::mutex mutex;       // 保护队列

    std::condition_variable cv;     // 唤醒等待线程

    std::atomic<bool> interrupted{ false };   // 打断标志

    std::atomic<int> maxSize{ 600 };          // 容量上限

    std::atomic<int64_t> dropped{ 0 };        // 丢弃计数

    // ---------- 直播时长上限（8.5，mutex 保护） ----------

    int liveDurationMs = 0;                   // 时长上限（0 = 关闭）

    bool durationTrimEnabled = true;          // 时长修剪开关（缓冲状态机用）

    size_t maxMemoryBytes = 0;                // 内存上限（0 = 关闭）

    size_t queueBytes = 0;                    // 当前队列字节数（packet->size 累计）

    AVRational liveTimeBase = { 1, 90000 };   // 包时间基

    // 直播时长修剪（调用者必须已持有 mutex）
    void TrimLiveLocked();
};

#pragma once

// ============================================================
// PacketQueue - 线程安全 AVPacket 队列（5.3 / 8.4 / 8.5）
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
//
// 8.5 直播模式（LiveMode）：
//   文件播放：希望全部播放 -> 队列完整缓存，满则背压
//   直播     ：希望追最新画面 -> 队列积压超过 maxDurationMs
//             就丢旧包（GOP 感知，保持解码链完整），Push 不阻塞
//
//   例：网络延迟 5 秒，直播不能继续播放 5 秒前的数据
//       -> Push 后按队首/队尾 pts 算队列时长，
//          queue_duration > 500ms 时丢最旧包，直到回到阈值内
//
//   注意：直播丢包必须 GOP 感知（README 8.4 教训）——
//         盲目丢最旧包会撕裂 GOP 导致花屏；
//         非关键帧丢到关键帧为止，队头是关键帧则整段清空。
// ============================================================

#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>

#include "Utils/FFmpegPtr.h"

extern "C" {
#include <libavutil/rational.h>
}

class PacketQueue
{
public:

    PacketQueue();

    ~PacketQueue();

    // 推入一个 Packet（移动语义，队列接管所有权）
    // maxSize: 队列最大 Packet 数量，达到后阻塞等待（谓词等待）
    //          LiveMode 下忽略：满则丢旧包，不阻塞
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

    // ---------- 直播模式（8.5） ----------

    // 开启 / 关闭直播模式（OpenMedia 时调用，默认关闭）
    //   enable       : true = 直播模式（追最新，不阻塞）
    //   timeBaseNum  : 包 time_base 分子（换算 pts -> 秒，通常来自流的 time_base）
    //   timeBaseDen  : 包 time_base 分母
    //   maxDurationMs: 队列允许积压的最大时长（默认 500ms），
    //                  超过则丢旧包（GOP 感知）
    void SetLiveMode(
        bool enable,
        int timeBaseNum,
        int timeBaseDen,
        int maxDurationMs);

    // 是否直播模式
    bool IsLiveMode() const;

    // 直播模式累计丢包数（统计：丢包率分子）
    int64_t GetDroppedCount() const;

private:

    // 直播模式修剪：队列时长超过 maxDurationMs 时丢旧包。
    // 调用者必须已持有 mutex。
    void TrimLiveLocked();

    std::queue<PacketPtr> queue;    // Packet 队列（unique 所有权）

    mutable std::mutex mutex;       // 保护队列

    std::condition_variable cv;     // 唤醒等待线程

    std::atomic<bool> interrupted;  // 打断标志

    // ---------- 直播模式状态（mutex 保护） ----------

    bool liveMode = false;          // 直播模式开关

    AVRational liveTimeBase = { 1, 90000 };  // 包时间基（pts -> 秒）

    int liveMaxDurationMs = 500;    // 允许积压的最大时长

    int64_t droppedCount = 0;       // 直播模式累计丢包数
};

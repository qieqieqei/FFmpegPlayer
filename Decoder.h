#pragma once

// ============================================================
// Decoder - 线程化解码器（5.2 / 5.3）
//
// 架构（5.3 线程升级）：
//
//   Demux Thread                Decode Thread             Render Thread
//   av_read_frame()             packetQueue.Pop()         frameQueue.Pop()
//        |                           |                         |
//        v                           v                         v
//   PacketQueue(视频)  ------>  avcodec_send_packet()    同步/渲染
//        |                           |
//        |                           v
//        +--音频包---> 回调Player    FrameQueue(视频)
//                     (解码/重采样/变速/推送)
//
// 职责：
//   - 打开输入文件，找到音视频流
//   - Demux 线程：读包，视频包入队，音频包回调给 Player 处理
//   - Decode 线程：视频包解码成帧，入 FrameQueue
//   - Seek（5.2）：Demux 线程内执行 avformat_seek_file，
//     清空队列、唤醒阻塞线程、通知解码线程 flush
//
// 队列被打断（Interrupt）说明正在 Seek / 退出，
// 各线程需要自行处理并等待恢复
// ============================================================

#include <atomic>
#include <functional>
#include <string>
#include <thread>

#include "Queue/PacketQueue.h"
#include "Queue/FrameQueue.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

// 队列容量上限（背压阈值）
static constexpr int MAX_VIDEO_PACKETS = 120;   // 视频包队列上限

static constexpr int MAX_VIDEO_FRAMES  = 12;    // 视频帧队列上限

class Decoder
{

public:

    Decoder();

    ~Decoder();

    // 打开输入文件、定位音视频流、打开视频解码器
    // 返回 false 表示失败
    bool Open(
        const std::string& path);

    // 启动 Demux / Decode 两个线程
    void Start();

    // 停止线程并等待结束（退出时调用）
    void Stop();

    // 请求 Seek 到指定时间（秒）——由 Demux 线程实际执行
    void RequestSeek(
        double seconds);

    bool HasSeekRequest() const;

    // Seek 是否已经执行完毕（渲染线程据此丢弃旧帧）
    bool IsSeekHandled() const;

    void ClearSeekHandled();

    // ---------- 音频回调（Player 注册，Demux 线程调用） ----------

    // 音频包回调：pkt 为 nullptr 时表示 EOF 冲刷（发送 NULL 包）
    // 回调负责 av_packet_free(pkt)
    void SetAudioPacketHandler(
        std::function<void(AVPacket*)> handler);

    // Seek 完成后的音频清理回调（清解码器/重采样器/变速器/时钟）
    void SetAudioSeekHandler(
        std::function<void(double)> handler);

    // ---------- 状态查询 ----------

    AVFormatContext* GetFormatContext() const;

    AVStream* GetVideoStream() const;

    AVStream* GetAudioStream() const;

    int GetVideoIndex() const;

    int GetAudioIndex() const;

    AVCodecContext* GetVideoCodecContext() const;

    PacketQueue& GetVideoPacketQueue();

    FrameQueue& GetVideoFrameQueue();

    double GetDuration() const;

    // Demux 是否已读到文件尾
    bool IsDemuxEof() const;

    // 视频解码是否已全部完成（含解码器冲刷）
    bool IsDecodeEof() const;

    // 当前 Seek 目标（秒）
    double GetSeekTarget() const;

private:

    // Demux 线程主循环
    void DemuxLoop();

    // Decode 线程主循环
    void DecodeLoop();

    // 执行 Seek（必须在 Demux 线程中调用）
    void DoSeek();

    // 关闭并释放所有资源（不含线程，线程需先 Stop）
    void Close();

    // 输入文件上下文
    AVFormatContext* fmt = nullptr;

    // 视频流 / 音频流索引
    int videoIndex = -1;

    int audioIndex = -1;

    // 视频解码器上下文（仅 Decode 线程访问）
    AVCodecContext* videoCodecCtx = nullptr;

    // 视频帧（解码线程复用）
    AVFrame* frame = nullptr;

    // 视频包队列（Demux -> Decode）
    PacketQueue videoPacketQueue;

    // 视频帧队列（Decode -> Render）
    FrameQueue videoFrameQueue;

    // 两个工作线程
    std::thread demuxThread;

    std::thread decodeThread;

    // 退出标志（Stop 时置位）
    std::atomic<bool> quit{ false };

    // Demux 是否 EOF
    std::atomic<bool> demuxEof{ false };

    // 视频解码是否全部完成
    std::atomic<bool> decodeEof{ false };

    // Seek 请求标志 + 目标时间
    std::atomic<bool> seekRequested{ false };

    std::atomic<double> seekTarget{ 0.0 };

    // Seek 已完成标志
    std::atomic<bool> seekHandled{ false };

    // Seek 代数：每次 DoSeek 递增，Decode 线程据此判断是否需要 flush
    std::atomic<int> seekGeneration{ 0 };

    // 音频包回调（Player 提供）
    std::function<void(AVPacket*)> audioPacketHandler;

    // Seek 完成后的音频清理回调（Player 提供）
    std::function<void(double)> audioSeekHandler;

};

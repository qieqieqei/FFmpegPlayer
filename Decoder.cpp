#include "Decoder.h"

#include "Utils/ErrorHandler.h"

#include <SDL.h>

#include <iostream>
#include <algorithm>

Decoder::Decoder()
{
}

Decoder::~Decoder()
{
    // 析构前确保线程已停止
    Stop();

    Close();
}

bool Decoder::Open(
    const std::string& path)
{
    // ---------- 打开输入文件 ----------

    int ret =
        avformat_open_input(
            &fmt,
            path.c_str(),
            nullptr,
            nullptr);

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Decoder,
            "avformat_open_input",
            ret);

        return false;
    }

    // 读取流信息（时长、码率等）
    ret =
        avformat_find_stream_info(
            fmt,
            nullptr);

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Decoder,
            "avformat_find_stream_info",
            ret);

        return false;
    }

    std::cout
        << "[Decoder] File : "
        << path
        << std::endl;

    std::cout
        << "[Decoder] Streams : "
        << fmt->nb_streams
        << std::endl;

    std::cout
        << "[Decoder] Duration : "
        << fmt->duration / static_cast<double>(AV_TIME_BASE)
        << " s"
        << std::endl;

    // ---------- 寻找视频流 / 音频流 ----------

    for (unsigned int i = 0; i < fmt->nb_streams; i++)
    {
        // 逐个检查每个流

        AVStream* stream = fmt->streams[i];

        // 流类型：视频 / 音频 / 字幕 ...

        AVMediaType type =
            stream->codecpar->codec_type;

        if (type == AVMEDIA_TYPE_VIDEO &&
            videoIndex < 0)
        {
            videoIndex = static_cast<int>(i);
        }
        else if (type == AVMEDIA_TYPE_AUDIO &&
            audioIndex < 0)
        {
            audioIndex = static_cast<int>(i);
        }
    }

    if (videoIndex < 0)
    {
        ErrorHandler::Log(
            ErrorTag::Decoder,
            "No video stream found");

        return false;
    }

    std::cout
        << "[Decoder] Video Stream Index : "
        << videoIndex
        << std::endl;

    if (audioIndex >= 0)
    {
        std::cout
            << "[Decoder] Audio Stream Index : "
            << audioIndex
            << std::endl;
    }
    else
    {
        std::cout
            << "[Decoder] No audio stream (video only)"
            << std::endl;
    }

    // ---------- 打开视频解码器 ----------

    const AVCodec* codec =
        avcodec_find_decoder(
            fmt->streams[videoIndex]->codecpar->codec_id);

    if (!codec)
    {
        ErrorHandler::Log(
            ErrorTag::Decoder,
            "Video decoder not found");

        return false;
    }

    videoCodecCtx =
        avcodec_alloc_context3(codec);

    if (!videoCodecCtx)
    {
        ErrorHandler::Log(
            ErrorTag::Decoder,
            "avcodec_alloc_context3 failed");

        return false;
    }

    ret =
        avcodec_parameters_to_context(
            videoCodecCtx,
            fmt->streams[videoIndex]->codecpar);

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Decoder,
            "avcodec_parameters_to_context",
            ret);

        return false;
    }

    ret =
        avcodec_open2(
            videoCodecCtx,
            codec,
            nullptr);

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Decoder,
            "avcodec_open2 (video)",
            ret);

        return false;
    }

    std::cout
        << "[Decoder] Video Codec : "
        << codec->name
        << " ("
        << videoCodecCtx->width
        << "x"
        << videoCodecCtx->height
        << ")"
        << std::endl;

    // 解码帧缓冲区
    frame =
        av_frame_alloc();

    if (!frame)
    {
        ErrorHandler::Log(
            ErrorTag::Decoder,
            "av_frame_alloc failed");

        return false;
    }

    return true;
}

void Decoder::Start()
{
    quit.store(false);

    // Demux 线程：读包 + 音频解码 + Seek 执行
    demuxThread =
        std::thread(
            &Decoder::DemuxLoop,
            this);

    // Decode 线程：视频解码
    decodeThread =
        std::thread(
            &Decoder::DecodeLoop,
            this);

    std::cout
        << "[Decoder] Threads Started (Demux + Decode)"
        << std::endl;
}

void Decoder::Stop()
{
    if (!demuxThread.joinable() &&
        !decodeThread.joinable())
    {
        return;
    }

    quit.store(true);

    // 打断所有阻塞调用，唤醒线程退出
    videoPacketQueue.Interrupt();

    videoFrameQueue.Interrupt();

    if (demuxThread.joinable())
    {
        demuxThread.join();
    }

    if (decodeThread.joinable())
    {
        decodeThread.join();
    }

    std::cout
        << "[Decoder] Threads Stopped"
        << std::endl;
}

void Decoder::RequestSeek(
    double seconds)
{
    if (seconds < 0.0)
    {
        seconds = 0.0;
    }

    // 记录 Seek 目标（由 Demux 线程消费）
    seekTarget.store(seconds);

    seekRequested.store(true);
}

bool Decoder::HasSeekRequest() const
{
    return seekRequested.load();
}

bool Decoder::IsSeekHandled() const
{
    return seekHandled.load();
}

void Decoder::ClearSeekHandled()
{
    seekHandled.store(false);
}

void Decoder::SetAudioPacketHandler(
    std::function<void(AVPacket*)> handler)
{
    audioPacketHandler = std::move(handler);
}

void Decoder::SetAudioSeekHandler(
    std::function<void(double)> handler)
{
    audioSeekHandler = std::move(handler);
}

AVFormatContext* Decoder::GetFormatContext() const
{
    return fmt;
}

AVStream* Decoder::GetVideoStream() const
{
    if (!fmt || videoIndex < 0)
    {
        return nullptr;
    }

    return fmt->streams[videoIndex];
}

AVStream* Decoder::GetAudioStream() const
{
    if (!fmt || audioIndex < 0)
    {
        return nullptr;
    }

    return fmt->streams[audioIndex];
}

int Decoder::GetVideoIndex() const
{
    return videoIndex;
}

int Decoder::GetAudioIndex() const
{
    return audioIndex;
}

AVCodecContext* Decoder::GetVideoCodecContext() const
{
    return videoCodecCtx;
}

PacketQueue& Decoder::GetVideoPacketQueue()
{
    return videoPacketQueue;
}

FrameQueue& Decoder::GetVideoFrameQueue()
{
    return videoFrameQueue;
}

double Decoder::GetDuration() const
{
    if (!fmt)
    {
        return 0.0;
    }

    return fmt->duration / static_cast<double>(AV_TIME_BASE);
}

bool Decoder::IsDemuxEof() const
{
    return demuxEof.load();
}

bool Decoder::IsDecodeEof() const
{
    return decodeEof.load();
}

double Decoder::GetSeekTarget() const
{
    return seekTarget.load();
}

// ============================================================
// Demux 线程主循环
//
//   1. 处理 Seek 请求（优先）
//   2. av_read_frame 读一个包
//   3. 视频包 -> videoPacketQueue
//   4. 音频包 -> 回调 Player（解码/重采样/变速/推送）
//   5. EOF -> 冲刷音频解码器
// ============================================================

void Decoder::DemuxLoop()
{
    while (!quit.load())
    {
        // ---------- Seek 处理（优先） ----------

        if (seekRequested.exchange(false))
        {
            DoSeek();
        }

        if (quit.load())
        {
            break;
        }

        // ---------- 读一个包 ----------

        // 8.4：PacketPtr RAII，所有权随包流转（Demux -> 队列 -> 解码器）
        PacketPtr pkt(
            av_packet_alloc());

        int ret =
            av_read_frame(
                fmt,
                pkt.get());

        if (ret < 0)
        {
            // pkt 作用域结束自动释放

            if (ret == AVERROR_EOF)
            {
                // 文件读完了：标记 EOF，冲刷音频解码器剩余帧
                demuxEof.store(true);

                if (audioPacketHandler)
                {
                    // nullptr 表示 EOF 冲刷
                    audioPacketHandler(nullptr);
                }
            }
            else
            {
                ErrorHandler::LogFFmpeg(
                    ErrorTag::Decoder,
                    "av_read_frame",
                    ret);
            }

            // 短暂等待，让 Seek 请求有机会被处理
            SDL_Delay(2);

            continue;
        }

        // ---------- 分发包 ----------

        if (pkt->stream_index == videoIndex)
        {
            // 视频包入队（Decode 线程消费）
            // 8.4：失败时 pkt 仍归本作用域，RAII 自动释放
            videoPacketQueue.Push(
                std::move(pkt),
                MAX_VIDEO_PACKETS);
        }
        else if (
            pkt->stream_index == audioIndex &&
            audioPacketHandler)
        {
            // 音频包交给 Player 处理（回调内负责释放）：
            // 8.4：release() 移交所有权，本作用域不再释放
            audioPacketHandler(pkt.release());
        }
        else
        {
            // 其他流（字幕等）直接丢弃（RAII 自动释放）
        }
    }
}

// ============================================================
// Decode 线程主循环
//
//   1. 从 videoPacketQueue 取包
//   2. avcodec_send_packet + avcodec_receive_frame
//   3. 帧入 videoFrameQueue（Render 线程消费）
//
//   Seek 时队列会被 Interrupt：
//   - 立即 flush 视频解码器（丢弃 Seek 前的解码状态）
//   - 等待队列恢复后继续
// ============================================================

void Decoder::DecodeLoop()
{
    // Seek 代数：每次 Seek 递增，用于判断是否需要 flush
    int lastSeekGen = 0;

    // 本 EOF 周期是否已冲刷过解码器
    bool eofFlushed = false;

    while (!quit.load())
    {
        // ---------- 检查是否有新的 Seek ----------

        int seekGen =
            seekGeneration.load();

        if (seekGen != lastSeekGen)
        {
            // 新的一次 Seek：清空解码器内部状态
            avcodec_flush_buffers(videoCodecCtx);

            lastSeekGen = seekGen;

            eofFlushed = false;

            // 解码未结束
            decodeEof.store(false);
        }

        // ---------- 取视频包 ----------

        // 8.4：PacketPtr 返回所有权，无需手动释放
        PacketPtr pkt =
            videoPacketQueue.Pop(50);

        if (!pkt)
        {
            if (videoPacketQueue.IsInterrupted())
            {
                // Seek / 退出中：等待队列恢复
                SDL_Delay(2);

                continue;
            }

            if (demuxEof.load())
            {
                // 队列取空且文件已读完：冲刷解码器剩余帧
                if (!eofFlushed)
                {
                    eofFlushed = true;

                    // 发送 NULL 包触发解码器冲刷
                    avcodec_send_packet(
                        videoCodecCtx,
                        nullptr);

                    // 取出所有剩余帧
                    while (true)
                    {
                        int recv =
                            avcodec_receive_frame(
                                videoCodecCtx,
                                frame);

                        if (recv == AVERROR(EAGAIN) ||
                            recv == AVERROR_EOF)
                        {
                            break;
                        }

                        if (recv < 0)
                        {
                            break;
                        }

                        // 克隆一帧入队（frame 会被复用）
                        // 8.4：FramePtr 接管克隆帧所有权
                        FramePtr out(
                            av_frame_clone(frame));

                        av_frame_unref(frame);

                        if (!out)
                        {
                            break;
                        }

                        // 失败时 out 作用域结束自动释放
                        videoFrameQueue.Push(
                            std::move(out),
                            MAX_VIDEO_FRAMES);
                    }
                }

                // 视频解码全部完成
                decodeEof.store(true);

                SDL_Delay(2);

                continue;
            }

            // 普通超时（暂时没数据），继续等
            continue;
        }

        if (videoPacketQueue.IsInterrupted())
        {
            // 取到的是 Seek 前的旧包，丢弃（RAII 自动释放）
            continue;
        }

        // ---------- 解码 ----------

        // 8.4：send 为同步消费，pkt 用后自动释放
        int ret =
            avcodec_send_packet(
                videoCodecCtx,
                pkt.get());

        if (ret < 0 &&
            ret != AVERROR(EAGAIN))
        {
            // 发送失败（不是需要重试的情况）
            ErrorHandler::LogFFmpeg(
                ErrorTag::Decoder,
                "avcodec_send_packet (video)",
                ret);

            continue;
        }

        // ---------- 取出所有解码出的帧 ----------

        while (true)
        {
            int recv =
                avcodec_receive_frame(
                    videoCodecCtx,
                    frame);

            if (recv == AVERROR(EAGAIN) ||
                recv == AVERROR_EOF)
            {
                // 需要更多包，或解码结束
                break;
            }

            if (recv < 0)
            {
                ErrorHandler::LogFFmpeg(
                    ErrorTag::Decoder,
                    "avcodec_receive_frame (video)",
                    recv);

                break;
            }

            // 克隆一帧入队（frame 会被复用）
            // 8.4：FramePtr 接管克隆帧所有权
            FramePtr out(
                av_frame_clone(frame));

            av_frame_unref(frame);

            if (!out)
            {
                break;
            }

            if (!videoFrameQueue.Push(
                std::move(out),
                MAX_VIDEO_FRAMES))
            {
                // 入队被打断（Seek/退出）：out 自动释放

                // 说明正在 Seek：flush 后等待恢复
                if (videoFrameQueue.IsInterrupted())
                {
                    avcodec_flush_buffers(videoCodecCtx);

                    lastSeekGen = seekGeneration.load();

                    break;
                }
            }

            // 有新数据到达，说明不再是 EOF 状态
            decodeEof.store(false);
        }
    }
}

// ============================================================
// 执行 Seek（Demux 线程）
//
// 流程：
//   1. 打断两个队列，唤醒阻塞中的 Decode 线程
//   2. avformat_seek_file 定位到目标时间
//   3. 清空队列里的旧数据
//   4. 恢复队列
//   5. 回调 Player 清理音频链路（解码器/重采样器/变速器/时钟）
//   6. 标记 seekHandled（渲染线程据此丢弃旧帧）
// ============================================================

void Decoder::DoSeek()
{
    double target =
        seekTarget.load();

    // 目标时间夹在 [0, 时长] 内
    target =
        std::max(
            0.0,
            std::min(
                GetDuration(),
                target));

    seekTarget.store(target);

    std::cout
        << "[Decoder] Seek -> "
        << target
        << " s"
        << std::endl;

    // ---------- 1. 打断队列，唤醒阻塞线程 ----------

    videoPacketQueue.Interrupt();

    videoFrameQueue.Interrupt();

    // ---------- 2. 定位文件（AV_TIME_BASE 时间基） ----------

    int64_t targetTs =
        static_cast<int64_t>(
            target * AV_TIME_BASE);

    int ret =
        avformat_seek_file(
            fmt,
            -1,                 // -1：按全局时间基（AV_TIME_BASE）定位
            INT64_MIN,          // 允许向后找任意位置
            targetTs,           // 目标时间戳
            targetTs,           // 最小目标（向后找关键帧）
            0);                 // 无特殊标志

    if (ret < 0)
    {
        // 备用方案：按视频流时间基定位
        AVStream* vStream =
            GetVideoStream();

        if (vStream)
        {
            int64_t streamTs =
                av_rescale_q(
                    targetTs,
                    AV_TIME_BASE_Q,
                    vStream->time_base);

            ret =
                av_seek_frame(
                    fmt,
                    videoIndex,
                    streamTs,
                    AVSEEK_FLAG_BACKWARD);
        }
    }

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Decoder,
            "Seek failed",
            ret);
    }

    // ---------- 3. 清空旧数据 ----------

    videoPacketQueue.Clear();

    videoFrameQueue.Clear();

    demuxEof.store(false);

    decodeEof.store(false);

    // ---------- 4. 恢复队列 ----------

    videoPacketQueue.ResetInterrupt();

    videoFrameQueue.ResetInterrupt();

    // ---------- 5. 清理音频链路（回调 Player） ----------

    if (audioSeekHandler)
    {
        audioSeekHandler(target);
    }

    // ---------- 6. 标记 Seek 完成 ----------

    seekHandled.store(true);

    // Seek 代数 +1（Decode 线程检测到变化会 flush 解码器）
    seekGeneration.fetch_add(1);

    std::cout
        << "[Decoder] Seek Done"
        << std::endl;
}

// ============================================================
// 关闭并释放资源
// ============================================================

void Decoder::Close()
{
    if (frame)
    {
        av_frame_free(&frame);
    }

    if (videoCodecCtx)
    {
        avcodec_free_context(&videoCodecCtx);
    }

    if (fmt)
    {
        avformat_close_input(&fmt);
    }
}

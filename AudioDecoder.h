#pragma once

#include "Utils/FFmpegPtr.h"

extern "C"
{

#include <libavcodec/avcodec.h>

}

// ============================================================
// AudioDecoder - 音频解码器
//
// 职责：
//   把 AVPacket 解码为 AVFrame(PCM)
//
//   SendPacket()  ->  avcodec_send_packet
//   ReceiveFrame()->  avcodec_receive_frame
//
// Seek 时需要调用 Flush() 清空解码器内部缓冲，
// 否则 Seek 后第一帧会解码出 Seek 前的数据
//
// 8.1：内部资源 RAII（AVCodecContextPtr / AVFramePtr）
// ============================================================

class AudioDecoder
{

public:

    AudioDecoder();

    ~AudioDecoder();

    bool Init(
        AVCodecParameters* codecpar);

    bool SendPacket(
        AVPacket* packet);

    AVFrame* ReceiveFrame();

    // 清空解码器内部缓冲（Seek 后调用）
    // 调用后需要重新 SendPacket 新位置的包
    void Flush();

    void Close();

private:

    AVCodecContextPtr codecCtx;   // 音频解码上下文（RAII）

    AVFramePtr frame;             // PCM音频帧（RAII）

};

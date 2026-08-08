#pragma once

// ============================================================
// VideoDecoder - 视频解码器（6.2 / 8.1 RAII）
//
// 职责：只负责
//
//   AVPacket
//      |
//      v
//   avcodec_send_packet()
//      |
//      v
//   avcodec_receive_frame()
//      |
//      v
//   AVFrame
//
// 线程归属：Decode 线程独占（不跨线程调用）
//
// 配合 FrameQueue 使用：
//   解码线程 ReceiveFrame() 后 av_frame_clone 一帧推入队列，
//   渲染线程从队列取帧，两者互不干扰
//
// 8.1：内部资源全部 RAII（FFmpegPtr），无需手动 Close 也不会泄漏
// ============================================================

#include "Utils/FFmpegPtr.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

class VideoDecoder
{
public:

    VideoDecoder();

    ~VideoDecoder();

    // 初始化：根据流的编码参数创建并打开解码器
    bool Init(
        AVCodecParameters* codecpar);

    // 送入一个待解码的包（调用者仍需负责释放 pkt）
    bool SendPacket(
        AVPacket* pkt);

    // 取出一帧解码结果
    // 返回内部复用帧（下次调用前有效），
    // 没有可用的帧时返回 nullptr（需要继续 SendPacket）
    AVFrame* ReceiveFrame();

    // 清空解码器内部缓冲（Seek 后调用，否则解出旧数据）
    void Flush();

    // 释放解码器（RAII 下通常无需手动；保留兼容）
    void Close();

    AVCodecContext* GetContext() const;

    int GetWidth() const;

    int GetHeight() const;

private:

    AVCodecContextPtr codecCtx;   // 视频解码上下文（RAII）

    AVFramePtr frame;             // 解码帧（复用，RAII）
};

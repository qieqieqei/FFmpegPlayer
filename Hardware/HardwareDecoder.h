#pragma once

// ============================================================
// HardwareDecoder - 硬件视频解码器（7.7）
//
// 基于 CUDAContext 的硬件加速解码（NVDEC / D3D11VA / DXVA2），
// 同时内置软解回退：
//
//   Init 流程：
//     avcodec_find_decoder("h264"/"hevc")
//       + hw_device_ctx（CUDAContext 提供）
//       + get_format 回调（让解码器选硬件像素格式）
//       + hw_frames_ctx（GPU 帧池）
//     -> 任一环节失败自动回退纯软解（IsHardware() == false）
//
//   ReceiveFrame 返回硬件帧（format = CUDA/D3D11/DXVA2_VLD）：
//     - 渲染 / 滤镜  : TransferFrame() 拷回系统内存（NV12）
//     - 硬编零拷贝   : 直接把 GPU 帧喂给 h264_nvenc
//                      （编码器复用 GetHardwareFramesRef()）
//
// 线程归属：解码线程独占（与 VideoDecoder 一致）。
//
// 8.1：内部资源 RAII（AVCodecContextPtr / AVFramePtr / AVBufferRefPtr）
// ============================================================

#include <string>

#include "Hardware/CUDAContext.h"
#include "Utils/FFmpegPtr.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

class HardwareDecoder
{
public:

    HardwareDecoder();

    ~HardwareDecoder();

    // 初始化解码器。
    // codecName : "h264" / "hevc"
    // codecpar  : 视频流编码参数（含 extradata / 宽高，必需）
    // 失败（含无硬件）时自动回退软解，返回是否至少可用
    bool Init(
        CUDAContext* cuda,
        const std::string& codecName,
        AVCodecParameters* codecpar);

    // 8.5：低延迟模式开关。必须在 Init 之前调用；
    // 开启后两处 avcodec_open2（硬件路径 + 软解回退）都传
    // flags=low_delay（AV_CODEC_FLAG_LOW_DELAY）。
    void SetLowDelay(
        bool enable);

    // 送入一个待解码的包（调用者仍需负责释放 pkt）
    bool SendPacket(
        AVPacket* pkt);

    // 取出一帧解码结果（内部复用帧，下次调用前有效）
    // 硬件模式返回 GPU 帧；软解模式返回普通帧
    AVFrame* ReceiveFrame();

    // 把硬件帧拷到系统内存帧（dst 由调用方管理）
    // 软解模式下等价于 av_frame_ref
    // 成功返回 true；dst 的格式为 sw_format（通常 NV12）
    bool TransferFrame(
        AVFrame* hwFrame,
        AVFrame* dst);

    // 清空解码器内部缓冲（Seek 后调用）
    void Flush();

    // 关闭并释放
    void Close();

    // 是否真正硬件解码（false = 软解回退）
    bool IsHardware() const;

    // 是否可用（硬件或软解）
    bool IsReady() const;

    AVCodecContext* GetContext() const;

    // 硬件帧池引用（零拷贝硬编时共享给编码器）
    // 返回内部引用，勿释放；无硬件时为 nullptr
    AVBufferRef* GetHardwareFramesRef() const;

private:

    // get_format 回调：解码器询问像素格式时优先返回硬件格式
    static AVPixelFormat OnGetFormat(
        AVCodecContext* ctx,
        const enum AVPixelFormat* pixFmts);

    AVCodecContextPtr codecCtx;   // 解码上下文（RAII）

    AVFramePtr frame;             // 解码帧（复用，RAII）

    CUDAContext* cuda = nullptr;          // 硬件上下文（借用）

    AVBufferRefPtr framesRef;     // hw_frames_ctx（RAII）

    AVPixelFormat hwPixFmt = AV_PIX_FMT_NONE;   // 硬件像素格式

    bool hardware = false;                // 是否硬解

    bool lowDelay = false;                // 8.5：解码级低延迟（AV_CODEC_FLAG_LOW_DELAY）
};

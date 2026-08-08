#pragma once

// ============================================================
// CUDAContext - GPU 硬件上下文（7.7）
//
// 管理硬件设备上下文（AVHWDeviceContext）的生命周期：
//   - 创建硬件设备（CUDA 优先，失败自动降级 D3D11VA / DXVA2）
//   - 提供设备上下文引用（供解码器 / 编码器共享）
//   - 提供硬件帧池（hw_frames_ctx）创建辅助
//
// 零拷贝管线（RTX4060）：
//
//   [NVDEC 硬解] --GPU 帧--> [h264_nvenc 硬编]
//        |                       |
//        +--av_hwframe_transfer_data--> 系统内存（渲染 / 滤镜）
//
//   解码器与编码器共享同一个 hw_frames_ctx，
//   帧数据全程留在显存，避免 GPU<->CPU 拷贝。
//
// 用法：
//   CUDAContext cuda;
//   if (!cuda.Init()) { /* 无可用硬件，回退软解 */ }
//   cuda.GetDeviceContext();        // 给解码器 hw_device_ctx
//   cuda.CreateFramesRef(w, h);     // 给解码器 hw_frames_ctx
//
// 8.1：deviceCtx 改为 AVBufferRefPtr（RAII）
// ============================================================

#include <string>

#include "Utils/FFmpegPtr.h"

extern "C" {
#include <libavutil/hwcontext.h>
}

class CUDAContext
{
public:

    CUDAContext();

    ~CUDAContext();

    // 创建硬件设备上下文。
    // 按 CUDA -> D3D11VA -> DXVA2 顺序尝试，
    // 全部失败返回 false（调用方回退软解）。
    bool Init();

    // 关闭并释放
    void Close();

    // 是否初始化成功
    bool IsAvailable() const;

    // 实际使用的设备类型（AV_HWDEVICE_TYPE_CUDA 等）
    AVHWDeviceType GetType() const;

    // 设备类型名（"cuda" / "d3d11va" / "dxva2"）
    std::string GetTypeName() const;

    // 设备上下文引用（av_buffer_ref 后使用，勿直接释放）
    AVBufferRef* GetDeviceContext() const;

    // 创建硬件帧池引用（解码器 hw_frames_ctx 用）
    // swFormat : 帧在显存中的软件采样格式（通常 NV12）
    // width/height : 视频尺寸
    // 返回的引用由调用方持有（av_buffer_unref 释放）
    AVBufferRef* CreateFramesRef(
        int width,
        int height,
        AVPixelFormat swFormat = AV_PIX_FMT_NV12) const;

    // 设备类型 -> 帧像素格式（解码器 / 编码器共用）
    static AVPixelFormat DeviceFormat(
        AVHWDeviceType type);

private:

    AVBufferRefPtr deviceCtx;   // 硬件设备上下文（RAII）

    AVHWDeviceType type = AV_HWDEVICE_TYPE_NONE;  // 实际类型
};

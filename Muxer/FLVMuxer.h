#pragma once

// ============================================================
// FLVMuxer - FLV 封装器（7.4）
//
// 用途：
//   - 本地 FLV 文件（out.flv）
//   - RTMP 推流（rtmp://...，FFmpeg 自动用 flv 封装 + rtmp 协议）
//     （RTMPPublisher 内部复用的正是本类）
//
// FLV 格式限制（务必遵守）：
//   视频只支持 H.264（AV_CODEC_ID_H264）
//   音频只支持 AAC（AV_CODEC_ID_AAC）
//   -> 编码器选型必须配套（h264_nvenc/libx264 + aac）
//
// 直播推流优化：
//   BeginWithKeyFrame(true) 时，文件头延迟到第一个视频关键帧
//   才写入 —— RTMP 接收端（如 ffplay/播放器）从关键帧开始
//   才能立即起播，避免黑屏等待。
// ============================================================

#include "Muxer/Muxer.h"

class FLVMuxer : public Muxer
{
public:

    FLVMuxer();

    // 打开输出（.flv 文件或 rtmp:// URL）
    bool OpenOutput(
        const std::string& url) override;

    // 写一个包。
    // 若启用了关键帧起始（BeginWithKeyFrame），
    // 关键帧到达前会先缓冲（视频包 + 音频包）
    bool WritePacket(
        AVPacket* pkt) override;

    // 关闭并释放
    void Close() override;

    // 设置"从关键帧开始写"（推流前调用，默认关闭）
    void BeginWithKeyFrame(
        bool enabled);

private:

    const char* GetFormatName() const override;

    bool beginWithKeyFrame = false;   // 关键帧起始

    std::vector<AVPacket*> pending;   // 起始缓冲（关键帧前）

    int pendingBytes = 0;             // 缓冲字节数（防爆）

    // 清空起始缓冲
    void ClearPending();

    static const int kMaxPendingBytes = 2 * 1024 * 1024;  // 2MB
};

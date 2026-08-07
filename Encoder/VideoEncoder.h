#pragma once

// ============================================================
// VideoEncoder - 视频编码器（7.4）
//
// 支持编码器（由 StreamConfig.video_codec 指定）：
//   libx264     软件 H.264（CPU，兼容性最好）
//   libx265     软件 H.265（CPU，压缩率最高）
//   h264_nvenc  硬件 H.264（RTX4060 NVENC，低延迟首选）
//
// 直播低延迟设置：
//   libx264  : tune=zerolatency（禁用 B 帧，编码即输出）
//   h264_nvenc: preset=ll + zerolatency=1 + bf=0
//
// 用法：
//   VideoEncoder enc;
//   enc.Init(1920, 1080, {1, 30}, "h264_nvenc", 4000, true);
//   enc.Encode(frame);            // 送原始帧（YUV420P）
//   AVPacket* pkt = enc.GetPacket();   // 取编码包（nullptr=暂无输出）
//   ... 交给 Muxer / RTMPPublisher ...
//
// 注意：
//   - 输入帧必须是 AV_PIX_FMT_YUV420P
//     （解码器输出其他格式时需先 sws_scale 转换）
//   - 硬编（nvenc）输入可来自 GPU 帧（hw_frames_ctx 直通，
//     见 HardwareDecoder，7.8 阶段实现零拷贝管线）
// ============================================================

#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/rational.h>
}

class VideoEncoder
{
public:

    VideoEncoder();

    ~VideoEncoder();

    // 初始化编码器
    // codecName : "libx264" / "libx265" / "h264_nvenc"
    // bitrateKbps : 目标码率（kbps）
    // live : 直播模式（低延迟：zerolatency / ll preset）
    bool Init(
        int width,
        int height,
        AVRational timeBase,     // 帧时间基，如 {1, 30}
        const std::string& codecName,
        int bitrateKbps,
        bool live = true);

    // 送一帧原始图像（YUV420P）进行编码
    // 成功返回 true；frame 由调用方管理（编码器内部会引用并立即拷贝）
    bool Encode(
        AVFrame* frame);

    // 取一个编码输出包（无输出时返回 nullptr）
    // 返回的包由调用方 av_packet_free 释放
    AVPacket* GetPacket();

    // 冲刷编码器尾帧（结束编码时调用，把缓冲帧全部输出）
    // 之后反复 GetPacket() 直到返回 nullptr
    void Flush();

    // 关闭并释放
    void Close();

    // 是否已初始化
    bool IsReady() const;

    // 编码器上下文（查询参数用）
    AVCodecContext* GetContext() const;

    // 编码器名称（实际使用的，如 "h264_nvenc"）
    const std::string& GetCodecName() const;

private:

    AVCodecContext* ctx = nullptr;   // 编码器上下文

    std::string codecName;           // 编码器名称

    bool ready = false;              // 初始化标志
};

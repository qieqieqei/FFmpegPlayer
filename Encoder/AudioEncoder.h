#pragma once

// ============================================================
// AudioEncoder - 音频编码器（7.4）
//
// 支持编码器（由 StreamConfig.audio_codec 指定）：
//   aac     AAC-LC（推荐：FLV / RTMP 只支持 AAC + H264）
//   opus    Opus（压缩率更高；⚠ 不能走 FLV/RTMP 封装）
//
// 输入：PCM 帧（任意采样格式，内部 swr 自动转成编码器所需格式）
//       典型输入：S16 / 48000Hz / 立体声（播放器解码输出格式）
// 输出：AAC / Opus 编码包（AVPacket）
//
// 用法：
//   AudioEncoder enc;
//   enc.Init(48000, 2, "aac", 128);
//   enc.Encode(frame);               // 送 PCM 帧
//   AVPacket* pkt = enc.GetPacket(); // 取编码包
//
// 注意：
//   - AAC 编码器要求 FLTP 采样格式，内部自动重采样
//   - Opus 编码器要求 48000Hz（强制）
//
// 8.1：ctx/swr/convFrame 全部 RAII（AVCodecContextPtr/SwrContextPtr/AVFramePtr）
// ============================================================

#include <string>

#include "Utils/FFmpegPtr.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/samplefmt.h>
}

class AudioEncoder
{
public:

    AudioEncoder();

    ~AudioEncoder();

    // 初始化编码器
    // codecName : "aac" / "opus"
    // bitrateKbps : 目标码率（AAC 默认 128，Opus 默认 96）
    bool Init(
        int sampleRate,
        int channels,
        const std::string& codecName,
        int bitrateKbps = 0);

    // 送一帧 PCM（任意采样格式，内部重采样为编码器所需格式）
    // frame 由调用方管理
    bool Encode(
        AVFrame* frame);

    // 取一个编码输出包（无输出时返回 nullptr）
    // 返回的包由调用方 av_packet_free 释放
    AVPacket* GetPacket();

    // 冲刷编码器尾帧（结束编码时调用）
    void Flush();

    // 关闭并释放
    void Close();

    // 是否已初始化
    bool IsReady() const;

    // 编码器上下文
    AVCodecContext* GetContext() const;

    // 编码器名称
    const std::string& GetCodecName() const;

private:

    // 把输入帧重采样成编码器所需格式
    // 成功返回转换后的帧（借用内部存储，调用方不要释放，
    // 会被 Encode 立即消费）；失败返回 nullptr
    AVFrame* ConvertFrame(
        AVFrame* frame);

    AVCodecContextPtr ctx;       // 编码器上下文（RAII）

    SwrContextPtr swr;           // 重采样器（RAII）

    AVFramePtr convFrame;        // 转换输出帧（内部复用，RAII）

    std::string codecName;           // 编码器名称

    int inSampleRate = 48000;        // 输入采样率

    int inChannels = 2;              // 输入声道数

    AVSampleFormat inFmt = AV_SAMPLE_FMT_S16;   // 输入采样格式

    bool ready = false;              // 初始化标志
};

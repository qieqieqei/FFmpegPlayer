#pragma once

// ============================================================
// Muxer - 封装器基类（7.4）
//
// 设计原则：保持薄。
//   avformat 的 alloc / write_header / write_packet / write_trailer
//   本身已提供多态，基类只做两件事：
//   1. 统一生命周期（OpenOutput / WritePacket / Close）
//   2. 提供公共工具（添加流 / 时间基转换）
//
// 派生类：
//   FLVMuxer   -> flv 封装（.flv 文件 或 rtmp:// 推流）
//   HLSMuxer   -> hls 封装（.m3u8 + 分段 .ts）
//
// 用法：
//   Muxer* muxer = new FLVMuxer();
//   muxer->OpenOutput("out.flv");
//   muxer->AddVideoStream(videoPar, {1,30});
//   muxer->AddAudioStream(audioPar);
//   muxer->WriteHeader();
//   muxer->WritePacket(pkt);   // 自动做时间基转换
//   muxer->WriteTrailer();
//   muxer->Close();
// ============================================================

#include <string>
#include <vector>

#include "Utils/FFmpegPtr.h"

extern "C" {
#include <libavformat/avformat.h>
}

class Muxer
{
public:

    Muxer();

    virtual ~Muxer();

    // 打开输出（分配输出上下文）
    // url : 输出路径，如 "out.flv" / "index.m3u8" / "rtmp://..."
    virtual bool OpenOutput(
        const std::string& url) = 0;

    // 添加视频流（codecpar 来自编码器上下文）
    // streamTimeBase : 输出流时间基（通常 = 编码器 time_base）
    AVStream* AddVideoStream(
        AVCodecParameters* codecpar,
        AVRational streamTimeBase);

    // 添加音频流
    AVStream* AddAudioStream(
        AVCodecParameters* codecpar);

    // 写文件头（必须在所有 AddStream 之后）
    bool WriteHeader();

    // 写一个包（自动把 pts/dts 从包时间基转到流时间基）
    // pkt 由调用方管理；本函数内部使用后即返回，不释放
    virtual bool WritePacket(
        AVPacket* pkt);

    // 写文件尾（结束写入）
    void WriteTrailer();

    // 关闭并释放
    virtual void Close();

    // 是否已打开
    bool IsOpen() const;

    // 输出上下文（查询参数用）
    AVFormatContext* GetFormatContext() const;

    // 输出 URL
    const std::string& GetUrl() const;

protected:

    // 派生类提供封装格式名（"flv" / "hls" / 空=按 URL 后缀）
    virtual const char* GetFormatName() const;

    AVFormatOutContextPtr fmt;  // 输出上下文（RAII，avformat_free_context）

    std::string url;                  // 输出 URL

    bool headerWritten = false;       // 文件头是否已写

    bool trailerWritten = false;      // 文件尾是否已写
};

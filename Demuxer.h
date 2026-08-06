#pragma once

// ============================================================
// Demuxer - 解复用器（6.1）
//
// 职责：
//   - avformat_open_input / avformat_find_stream_info
//   - 定位视频流 / 音频流
//   - av_read_frame 读包（Demux 线程）
//   - Seek 定位（avformat_seek_file）
//
// 线程架构：
//
//   Demux Thread
//     av_read_frame()
//        |
//        +--视频包--> videoPacketQueue
//        +--音频包--> audioPacketQueue
//
// 只管"拆包"，不管解码：
//   视频解码 -> VideoDecoder（Decode 线程）
//   音频解码 -> AudioDecoder（Audio 线程）
// ============================================================

#include <string>

extern "C" {
#include <libavformat/avformat.h>
}

class Demuxer
{
public:

    Demuxer();

    ~Demuxer();

    // 打开输入文件，解析流信息
    bool Open(
        const std::string& path);

    // 读一个包（内部 av_read_frame）
    // 返回 0 = 成功；AVERROR_EOF = 文件读完；<0 = 出错
    int ReadPacket(
        AVPacket* pkt);

    // 定位到指定时间（秒）
    // 先按全局时间基 avformat_seek_file，失败回退视频流时间基
    bool Seek(
        double seconds);

    // 关闭并释放输入文件
    void Close();

    // ---------- 流信息查询 ----------

    AVFormatContext* GetFormatContext() const;

    AVStream* GetVideoStream() const;

    AVStream* GetAudioStream() const;

    int GetVideoIndex() const;

    int GetAudioIndex() const;

    bool HasAudio() const;

    // 媒体时长（秒）
    double GetDuration() const;

private:

    AVFormatContext* fmt = nullptr;    // 输入文件上下文

    int videoIndex = -1;               // 视频流索引

    int audioIndex = -1;               // 音频流索引
};

#pragma once

// ============================================================
// Demuxer - 解复用器（6.1 / 7.1 重构）
//
// 职责：
//   - 通过 InputSource 打开输入（文件 / 网络流统一入口）
//   - 定位视频流 / 音频流
//   - av_read_frame 读包（Demux 线程）
//   - Seek 定位（仅点播流可用，直播自动禁用）
//
// 线程架构：
//
//   Demux Thread
//     av_read_frame()
//        |
//        +--视频包--> videoPacketQueue
//        +--音频包--> audioPacketQueue
//
// 输入层（7.1）：
//   Demuxer
//      |
//      +-- InputSource (工厂按 URL 协议创建)
//            |-- FileInput     : test.mp4 / file://...
//            +-- NetworkInput  : rtsp:// rtmp:// http(s)://
//
// 只管"拆包"，不管解码：
//   视频解码 -> VideoDecoder（Decode 线程）
//   音频解码 -> AudioDecoder（Audio 线程）
// ============================================================

#include <string>

#include "Input/InputSource.h"
#include "Config/StreamConfig.h"

class Demuxer
{
public:

    Demuxer();

    ~Demuxer();

    // 打开输入（本地路径或网络 URL，按协议自动选择输入源）
    bool Open(
        const std::string& url);

    // 读一个包（内部 av_read_frame）
    // 返回 0 = 成功；AVERROR_EOF = 流读完；<0 = 出错
    int ReadPacket(
        AVPacket* pkt);

    // 定位到指定时间（秒）
    // 直播流（RTSP/RTMP/直播 HLS）不可 Seek，返回 false
    bool Seek(
        double seconds);

    // 关闭并释放输入
    void Close();

    // 打断阻塞中的网络读取（退出 / 切换媒体时调用）
    void SetAbort(
        bool abort);

    // 网络参数（rtsp_transport / 超时 / 低延迟等）
    // 必须在 Open 之前调用；不调用则使用内置默认值
    void SetNetworkConfig(
        const StreamConfig& config);

    // ---------- 流信息查询 ----------

    AVFormatContext* GetFormatContext() const;

    AVStream* GetVideoStream() const;

    AVStream* GetAudioStream() const;

    int GetVideoIndex() const;

    int GetAudioIndex() const;

    bool HasAudio() const;

    // 媒体时长（秒）；直播流为 0 或未知
    double GetDuration() const;

    // ---------- 输入类型查询（7.1） ----------

    // 是否网络流
    bool IsNetwork() const;

    // 是否实时流（直播：缓冲策略 / Seek 行为不同）
    bool IsLive() const;

    // 是否可 Seek（点播流）
    bool IsSeekable() const;

    // 协议名："file" / "rtsp" / "rtmp" / "http" / "https"
    const std::string& GetProtocol() const;

    // 原始 URL
    const std::string& GetUrl() const;

private:

    InputSource* source = nullptr;      // 输入源（7.1）

    StreamConfig networkConfig;         // 网络参数（Open 前设置）

    bool networkConfigApplied = false;  // 是否已设置网络参数

    int videoIndex = -1;                // 视频流索引

    int audioIndex = -1;                // 音频流索引
};

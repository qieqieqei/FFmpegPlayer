#pragma once

// ============================================================
// InputSource - 统一输入接口（7.1）
//
// 所有输入源（本地文件 / 网络流）的抽象基类：
//
//                    InputSource
//                        |
//            ---------------------------
//            |                         |
//        FileInput               NetworkInput
//     （MP4/MKV/AVI 本地）     （RTSP/RTMP/HTTP/HLS）
//
// 统一接口：
//   Open(url)             打开输入
//   GetFormatContext()    获取 AVFormatContext
//   Close()               关闭释放
//
// 工厂：
//   InputSource::Create(url, cfg)
//   根据 URL 协议自动创建 FileInput / NetworkInput
//
// 中断回调：
//   网络流阻塞在 av_read_frame 时，SetAbort(true) 可打断，
//   保证退出 / 切换媒体不会卡死（对文件流同样生效）
//
// 8.1：fmt 改为 AVFormatContextPtr（RAII）
// ============================================================

#include <string>
#include <atomic>

#include "Utils/FFmpegPtr.h"

extern "C" {
#include <libavformat/avformat.h>
}

struct StreamConfig;

class InputSource
{
public:

    InputSource();

    virtual ~InputSource();

    // 打开输入源（文件路径或网络 URL）
    virtual bool Open(
        const std::string& url) = 0;

    // 关闭并释放
    virtual void Close();

    // 重连（网络流可重连；文件流默认失败）
    virtual bool Reconnect();

    // ---------- 属性查询 ----------

    // 是否网络流
    virtual bool IsNetwork() const;

    // 是否支持断线重连
    virtual bool IsReconnectable() const;

    // 是否实时流（RTSP/RTMP/直播 HLS：不可 Seek，缓冲要小）
    virtual bool IsLive() const;

    // 是否可 Seek（本地文件 / VOD 可；直播不可）
    virtual bool IsSeekable() const;

    // 协议名："file" / "rtsp" / "rtmp" / "http" / "https"
    const std::string& GetProtocol() const;

    // 原始 URL（含协议前缀）
    const std::string& GetUrl() const;

    // ---------- 控制 ----------

    // 打断阻塞中的网络读取（退出 / 切换媒体时调用）
    void SetAbort(
        bool abort);

    AVFormatContext* GetFormatContext() const;

    // ---------- 工厂 ----------

    // 根据 URL 协议创建对应输入源
    // cfg 为空指针时使用内置默认网络参数
    static InputSource* Create(
        const std::string& url,
        const StreamConfig* cfg = nullptr);

protected:

    // 打开 + 绑定中断回调 + 读取流信息
    // opts 为网络协议选项（可空）；成功/失败后由调用方负责 av_dict_free(*opts)
    bool OpenWithOptions(
        const std::string& url,
        AVDictionary** opts);

    // 中断回调（FFmpeg 阻塞 I/O 期间周期性调用）
    static int InterruptCallback(
        void* opaque);

    AVFormatContextPtr fmt;       // 输入上下文（打开后有效，RAII）

    std::atomic<bool> abort{ false };     // 中断标志

    std::string url;                      // 原始 URL

    std::string protocol = "file";        // 协议名

    bool live = false;                    // 是否实时流

    bool seekable = true;                 // 是否可 Seek
};

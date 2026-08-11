#include "Input/CameraInput.h"

#include "Utils/Logger.h"

// ============================================================
// CameraInput - 摄像头输入（8.5）
// ============================================================

CameraInput::CameraInput()
{
    // 恒为 RTSP 直播源
    protocol = "rtsp";

    live = true;

    seekable = false;
}

CameraInput::~CameraInput()
{
    Close();
}

bool CameraInput::IsNetwork() const
{
    return true;
}

bool CameraInput::IsReconnectable() const
{
    return true;
}

bool CameraInput::IsLive() const
{
    return true;
}

bool CameraInput::IsSeekable() const
{
    // 摄像头流不可 Seek（即使个别服务器上报 duration > 0）
    return false;
}

void CameraInput::SetConfig(
    const StreamConfig& config)
{
    cfg = config;
}

void CameraInput::SetLatencyMs(
    int ms)
{
    latencyMs = ms > 0 ? ms : 0;
}

void CameraInput::SetTransport(
    const std::string& transport)
{
    cfg.rtspTransport = transport;
}

int CameraInput::GetReconnectCount() const
{
    return reconnectCount;
}

bool CameraInput::Open(
    const std::string& url)
{
    reconnectCount = 0;

    Logger::Info()
        << "[CameraInput] Open : "
        << url
        << " (transport="
        << (cfg.rtspTransport.empty() ?
            "tcp" :
            cfg.rtspTransport)
        << ", latency="
        << (latencyMs > 0 ?
            std::to_string(latencyMs) + "ms" :
            "min(0)")
        << ")"
        << std::endl;

    // ---------- 构建低延迟选项字典 ----------

    AVDictionary* opts = nullptr;

    BuildOptions(&opts);

    // ---------- 打开（OpenWithOptions 内部绑定中断回调 + 读流信息） ----------

    bool ok =
        OpenWithOptions(
            url,
            &opts);

    // 剩余未识别选项在这里释放
    av_dict_free(&opts);

    if (!ok)
    {
        return false;
    }

    Logger::Info()
        << "[CameraInput] Opened : "
        << url
        << " [Live] [NoSeek]"
        << std::endl;

    return true;
}

void CameraInput::Close()
{
    // RAII：FFmpegPtr 自动 avformat_close_input
    fmt.reset();
}

bool CameraInput::Reconnect()
{
    if (url.empty() ||
        abort.load())
    {
        return false;
    }

    Logger::Warn()
        << "[CameraInput] Reconnecting : "
        << url
        << std::endl;

    // 释放旧连接（中断回调下次 Open 自动复位）
    Close();

    reconnectCount++;

    return Open(url);
}

void CameraInput::BuildOptions(
    AVDictionary** opts) const
{
    // ---------- 低延迟三件套（8.5） ----------
    //
    // 直播和文件播放不同：MP4 可以预缓冲，直播不行。
    // 直播的目标是追最新画面，积压的旧数据必须尽快丢。

    // 1) fflags=nobuffer：不预读缓冲，边收边播（减少起播延迟）
    av_dict_set(
        opts,
        "fflags",
        "nobuffer",
        0);

    // 2) flags=low_delay：低延迟模式。
    //    注意：AVFormatContext 本身没有 "flags" 选项，
    //    会被 avformat_open_input 静默忽略（不报错），
    //    真正的解码级 low_delay（AV_CODEC_FLAG_LOW_DELAY）
    //    由 Player 在 avcodec_open2 时通过 VideoDecoder::SetLowDelay 设置。
    //    此处按需求原样保留，保证字典完整。
    av_dict_set(
        opts,
        "flags",
        "low_delay",
        0);

    // 3) rtsp_transport=tcp：TCP 传输。
    //    UDP 丢包会导致花屏 / 音画撕裂，摄像头默认走 TCP。
    av_dict_set(
        opts,
        "rtsp_transport",
        cfg.rtspTransport.empty() ?
            "tcp" :
            cfg.rtspTransport.c_str(),
        0);

    // ---------- 延迟配置 ----------
    // latencyMs > 0：max_delay = latencyMs * 1000（微秒）
    // 默认 0：极限低延迟（RTSP 服务器不下发抖动缓冲）
    std::string maxDelay =
        latencyMs > 0 ?
        std::to_string(
            static_cast<long long>(latencyMs) *
            1000LL) :
        "0";

    av_dict_set(
        opts,
        "max_delay",
        maxDelay.c_str(),
        0);

    // ---------- 超时（毫秒 -> 微秒） ----------

    if (cfg.networkTimeoutMs > 0)
    {
        std::string us =
            std::to_string(
                static_cast<long long>(
                    cfg.networkTimeoutMs) *
                1000LL);

        av_dict_set(
            opts,
            "rw_timeout",
            us.c_str(),
            0);
    }

    if (cfg.rtspTimeoutMs > 0)
    {
        std::string us =
            std::to_string(
                static_cast<long long>(
                    cfg.rtspTimeoutMs) *
                1000LL);

        av_dict_set(
            opts,
            "stimeout",
            us.c_str(),
            0);
    }

    // ---------- 快速起播 ----------
    // 1.5s 探测窗口 + 300KB 上限（项目已验证的成熟参数）：
    // 摄像头起播更快，同时避免大探测窗口吃掉低延迟优势
    av_dict_set(
        opts,
        "analyzeduration",
        "1500000",
        0);

    av_dict_set(
        opts,
        "probesize",
        "300000",
        0);
}

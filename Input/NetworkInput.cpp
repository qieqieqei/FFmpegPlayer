#include "Input/NetworkInput.h"

#include "Utils/Logger.h"

#include <cctype>

// ============================================================
// NetworkInput - 网络输入
// ============================================================

NetworkInput::NetworkInput()
{
    protocol = "network";
}

NetworkInput::~NetworkInput()
{
    Close();
}

bool NetworkInput::IsNetwork() const
{
    return true;
}

bool NetworkInput::IsReconnectable() const
{
    // 网络流均可重连
    return true;
}

int NetworkInput::GetReconnectCount() const
{
    return reconnectCount;
}

void NetworkInput::SetConfig(
    const StreamConfig& config)
{
    cfg = config;
}

// ============================================================
// 打开网络流
// ============================================================

bool NetworkInput::Open(
    const std::string& url)
{
    // 识别协议：rtsp / rtmp / http / https
    protocol =
        url.substr(
            0,
            url.find("://"));

    // 统一小写
    for (auto& c : protocol)
    {
        c = static_cast<char>(
            std::tolower(
                static_cast<unsigned char>(c)));
    }

    reconnectCount = 0;

    Logger::Info()
        << "[NetworkInput] Open : "
        << url
        << " (protocol : "
        << protocol
        << ")"
        << std::endl;

    // ---------- 构建协议选项 ----------

    AVDictionary* opts = nullptr;

    BuildOptions(&opts);

    bool ok =
        OpenWithOptions(
            url,
            &opts);

    // avformat_open_input 消费选项后，剩余选项需手动释放
    av_dict_free(&opts);

    if (!ok)
    {
        return false;
    }

    Logger::Info()
        << "[NetworkInput] Opened : "
        << url
        << (live ?
            " [Live]" :
            " [VOD]")
        << (seekable ?
            " [Seekable]" :
            " [NoSeek]")
        << std::endl;

    return true;
}

void NetworkInput::Close()
{
    // RAII：AVFormatContextPtr 自动 avformat_close_input
    fmt.reset();
}

// ============================================================
// 断线重连
// ============================================================

bool NetworkInput::Reconnect()
{
    if (url.empty() ||
        abort.load())
    {
        return false;
    }

    Logger::Warn()
        << "[NetworkInput] Reconnecting : "
        << url
        << std::endl;

    Close();

    reconnectCount++;

    // 重新打开（Open 会复位 abort 标志）
    return Open(url);
}

// ============================================================
// 按协议构建选项字典
// ============================================================

void NetworkInput::BuildOptions(
    AVDictionary** opts) const
{
    // ---------- 通用：读写超时（微秒） ----------

    if (cfg.networkTimeoutMs > 0)
    {
        std::string timeoutUs =
            std::to_string(
                cfg.networkTimeoutMs * 1000LL);

        // rw_timeout：tcp/http/rtsp 等大多数协议支持
        av_dict_set(
            opts,
            "rw_timeout",
            timeoutUs.c_str(),
            0);
    }

    // ---------- 按协议细化 ----------

    if (protocol == "rtsp")
    {
        // TCP 传输：避免 UDP 丢包导致的马赛克
        if (!cfg.rtspTransport.empty())
        {
            av_dict_set(
                opts,
                "rtsp_transport",
                cfg.rtspTransport.c_str(),
                0);
        }

        // RTSP 专用连接超时（微秒）
        if (cfg.rtspTimeoutMs > 0)
        {
            std::string timeoutUs =
                std::to_string(
                    cfg.rtspTimeoutMs * 1000LL);

            av_dict_set(
                opts,
                "stimeout",
                timeoutUs.c_str(),
                0);
        }
    }
    else if (protocol == "http" ||
        protocol == "https")
    {
        // HTTP 连接超时（微秒）
        if (cfg.networkTimeoutMs > 0)
        {
            std::string timeoutUs =
                std::to_string(
                    cfg.networkTimeoutMs * 1000LL);

            av_dict_set(
                opts,
                "timeout",
                timeoutUs.c_str(),
                0);
        }
    }

    // ---------- 低延迟模式（直播默认开启） ----------

    if (cfg.lowLatency)
    {
        // 不预读缓冲：边收边播
        av_dict_set(
            opts,
            "fflags",
            "nobuffer",
            0);

        // 不累积延迟
        av_dict_set(
            opts,
            "max_delay",
            "0",
            0);

        // 快速起播：减少探测深度（但不能为 0）
        //   analyzeduration=0 会导致 HLS/TS 音频流参数（采样率/声道）
        //   分析不完整，后续音频初始化崩溃（0xC0000005）
        //   1.5s 探测窗口 + 300KB 探测上限：起播快且参数完整
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
}

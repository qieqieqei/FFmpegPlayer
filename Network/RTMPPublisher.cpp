#include "Network/RTMPPublisher.h"

#include "Utils/ErrorHandler.h"
#include "Utils/Logger.h"

#include <thread>
#include <chrono>

// ============================================================
// RTMPPublisher - RTMP 推流器
// ============================================================

RTMPPublisher::RTMPPublisher()
{
}

RTMPPublisher::~RTMPPublisher()
{
    Stop();
}

void RTMPPublisher::SetConfig(
    const StreamConfig& config)
{
    cfg = config;
}

bool RTMPPublisher::Connect(
    const std::string& url)
{
    Stop();

    this->url = url;

    failCount = 0;

    pushedPackets = 0;

    pushedBytes = 0;

    Logger::Info()
        << "[RTMPPublisher] Connect : "
        << url
        << std::endl;

    // RTMP 推流 = flv 封装 + rtmp 协议
    connected =
        muxer.OpenOutput(url);

    if (connected)
    {
        Logger::Info()
            << "[RTMPPublisher] Connected : "
            << url
            << std::endl;
    }
    else
    {
        failCount++;

        Logger::Error()
            << "[RTMPPublisher] Connect failed : "
            << url
            << std::endl;
    }

    return connected;
}

bool RTMPPublisher::AddVideoStream(
    AVCodecParameters* codecpar,
    AVRational streamTimeBase)
{
    if (!connected)
    {
        return false;
    }

    return
        muxer.AddVideoStream(
            codecpar,
            streamTimeBase) != nullptr;
}

bool RTMPPublisher::AddAudioStream(
    AVCodecParameters* codecpar)
{
    if (!connected)
    {
        return false;
    }

    return
        muxer.AddAudioStream(
            codecpar) != nullptr;
}

bool RTMPPublisher::Start()
{
    if (!connected)
    {
        return false;
    }

    // 从关键帧开始推送：接收端（ffplay/VLC）立即起播
    muxer.BeginWithKeyFrame(true);

    Logger::Info()
        << "[RTMPPublisher] Start pushing (key frame start)"
        << std::endl;

    return true;
}

bool RTMPPublisher::PushPacket(
    AVPacket* pkt)
{
    if (!connected || !pkt)
    {
        return false;
    }

    bool ok =
        muxer.WritePacket(pkt);

    if (!ok)
    {
        failCount++;

        Logger::Warn()
            << "[RTMPPublisher] Push failed ("
            << failCount
            << "), connection broken ?"
            << std::endl;

        return false;
    }

    // 失败后成功一次即清零
    failCount = 0;

    pushedPackets++;

    pushedBytes += pkt->size;

    return true;
}

void RTMPPublisher::Stop()
{
    if (!connected)
    {
        return;
    }

    Logger::Info()
        << "[RTMPPublisher] Stop pushing : "
        << url
        << std::endl;

    muxer.WriteTrailer();

    muxer.Close();

    connected = false;
}

void RTMPPublisher::Disconnect()
{
    Stop();
}

bool RTMPPublisher::Reconnect()
{
    if (url.empty())
    {
        return false;
    }

    // 超过最大重连次数（0 = 无限重连）
    if (cfg.reconnectMaxAttempts > 0 &&
        failCount >= cfg.reconnectMaxAttempts)
    {
        Logger::Error()
            << "[RTMPPublisher] Reconnect give up : "
            << url
            << " (fails : "
            << failCount
            << ")"
            << std::endl;

        return false;
    }

    // 等待重连间隔
    if (cfg.reconnectDelayMs > 0)
    {
        Logger::Warn()
            << "[RTMPPublisher] Reconnect in "
            << cfg.reconnectDelayMs
            << " ms"
            << std::endl;

        std::this_thread::sleep_for(
            std::chrono::milliseconds(
                cfg.reconnectDelayMs));
    }

    // 断开旧连接，重新连接
    muxer.Close();

    connected =
        muxer.OpenOutput(url);

    if (connected)
    {
        failCount = 0;

        Logger::Info()
            << "[RTMPPublisher] Reconnect success : "
            << url
            << std::endl;
    }
    else
    {
        failCount++;

        Logger::Warn()
            << "[RTMPPublisher] Reconnect failed ("
            << failCount
            << ") : "
            << url
            << std::endl;
    }

    return connected;
}

bool RTMPPublisher::IsConnected() const
{
    return connected;
}

int64_t RTMPPublisher::GetPushedPackets() const
{
    return pushedPackets;
}

int64_t RTMPPublisher::GetPushedBytes() const
{
    return pushedBytes;
}

int RTMPPublisher::GetFailCount() const
{
    return failCount;
}

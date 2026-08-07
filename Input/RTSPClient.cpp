#include "Input/RTSPClient.h"

#include "Utils/Logger.h"

#include <thread>
#include <chrono>

// ============================================================
// RTSPClient - RTSP 连接管理
// ============================================================

RTSPClient::RTSPClient()
{
}

bool RTSPClient::Connect(
    const std::string& url)
{
    this->url = url;

    attempts = 0;

    Logger::Info()
        << "[RTSPClient] Connect : "
        << url
        << std::endl;

    input.SetConfig(cfg);

    connected =
        input.Open(url);

    if (!connected)
    {
        attempts++;

        Logger::Warn()
            << "[RTSPClient] Connect failed : "
            << url
            << std::endl;
    }

    return connected;
}

void RTSPClient::Disconnect()
{
    if (connected)
    {
        Logger::Info()
            << "[RTSPClient] Disconnect : "
            << url
            << std::endl;
    }

    input.Close();

    connected = false;
}

bool RTSPClient::Reconnect()
{
    if (url.empty())
    {
        return false;
    }

    // 超过最大重连次数（0 = 无限重连）
    if (cfg.reconnectMaxAttempts > 0 &&
        attempts >= cfg.reconnectMaxAttempts)
    {
        Logger::Error()
            << "[RTSPClient] Reconnect give up : "
            << url
            << " (attempts : "
            << attempts
            << ")"
            << std::endl;

        return false;
    }

    attempts++;

    // 等待重连间隔
    if (cfg.reconnectDelayMs > 0)
    {
        Logger::Warn()
            << "[RTSPClient] Reconnect in "
            << cfg.reconnectDelayMs
            << " ms (attempt "
            << attempts
            << ") : "
            << url
            << std::endl;

        std::this_thread::sleep_for(
            std::chrono::milliseconds(
                cfg.reconnectDelayMs));
    }

    // 释放旧连接（内部会 avformat_close_input）
    input.Close();

    connected = false;

    Logger::Warn()
        << "[RTSPClient] Reconnecting : "
        << url
        << std::endl;

    input.SetConfig(cfg);

    connected =
        input.Open(url);

    if (connected)
    {
        // 重连成功：重置失败计数
        attempts = 0;

        Logger::Info()
            << "[RTSPClient] Reconnect success : "
            << url
            << std::endl;
    }

    return connected;
}

bool RTSPClient::IsConnected() const
{
    return connected;
}

int RTSPClient::GetConnectAttempts() const
{
    return attempts;
}

const std::string& RTSPClient::GetUrl() const
{
    return url;
}

AVFormatContext* RTSPClient::GetFormatContext() const
{
    return input.GetFormatContext();
}

void RTSPClient::SetAbort(
    bool abort)
{
    input.SetAbort(abort);
}

void RTSPClient::SetConfig(
    const StreamConfig& config)
{
    cfg = config;
}

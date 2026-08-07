#pragma once

// ============================================================
// RTSPClient - RTSP 连接管理（7.2）
//
// 职责：
//   - 管理 RTSP 连接（连接 / 断开 / 重连）
//   - 网络超时（由 NetworkInput 的 stimeout / rw_timeout 实现）
//   - 断线自动重连（次数上限 + 重连间隔，来自 StreamConfig）
//
// 架构：组合 NetworkInput（网络协议细节在 NetworkInput 中），
//       RTSPClient 只做"连接生命周期"这一层：
//
//   RTSPClient
//      |
//      +-- NetworkInput  (avformat_open_input + rtsp 选项)
//      |
//      +-- 重连策略：attempts / maxAttempts / delay
//
// 用法：
//   RTSPClient client;
//   client.SetConfig(streamConfig);
//   if (client.Connect("rtsp://192.168.1.100/live")) { ... }
//   client.Reconnect();          // 断线后重连
//   client.Disconnect();
// ============================================================

#include <string>

#include "Input/NetworkInput.h"
#include "Config/StreamConfig.h"

class RTSPClient
{
public:

    RTSPClient();

    // 连接 RTSP 地址（rtsp://ip:port/path）
    bool Connect(
        const std::string& url);

    // 断开连接并释放资源
    void Disconnect();

    // 断线重连（超过最大次数返回 false）
    // 内部：等待 reconnectDelayMs -> 关闭旧连接 -> 重新打开
    bool Reconnect();

    // ---------- 状态查询 ----------

    bool IsConnected() const;

    // 已尝试的连接次数（统计用）
    int GetConnectAttempts() const;

    // 原始 URL
    const std::string& GetUrl() const;

    // 输入上下文（连接成功后有效，供 Demuxer 读取流）
    AVFormatContext* GetFormatContext() const;

    // 打断阻塞中的网络读取（退出时调用）
    void SetAbort(
        bool abort);

    // 设置重连策略（连接前调用；不调用则使用默认值）
    void SetConfig(
        const StreamConfig& config);

private:

    NetworkInput input;             // 网络输入（组合）

    StreamConfig cfg;               // 网络参数

    std::string url;                // RTSP 地址

    bool connected = false;         // 连接状态

    int attempts = 0;               // 连续失败次数
};

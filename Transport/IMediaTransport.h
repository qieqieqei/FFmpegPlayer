#pragma once

// ============================================================
// IMediaTransport - 媒体传输抽象接口（9.0，评审意见）
//
// 职责：网络媒体数据的统一传输抽象（UDP / TCP / 后续扩展）。
//   - Open / Close 生命周期
//   - Send / Recv 数据收发（支持超时）
//   - 传输统计（字节 / 包 / 错误）
//
// 设计：
//   - 与解码 / 播放层解耦；RTP 层之上的乱序与抖动处理由
//     PacketReorderBuffer / JitterBuffer 完成（Transport 目录）
//   - UdpTransport 为参考实现（Winsock，见 UdpTransport.h）
//   - 不做 WebRTC / QUIC / RTM（评审明确排除）
// ============================================================

#include <cstdint>
#include <string>

// 传输配置
struct TransportConfig
{
    std::string remoteHost;       // 远端地址（IP / 域名）
    int remotePort = 0;           // 远端端口
    int localPort = 0;            // 本地绑定端口（0 = 系统分配）
    int recvBufferSize = 0;       // 接收缓冲区（字节，0 = 系统默认）
    int sendBufferSize = 0;       // 发送缓冲区（字节，0 = 系统默认）
    int connectTimeoutMs = 3000;  // 连接 / 首包超时（毫秒）
    bool reuseAddress = true;     // SO_REUSEADDR
};

// 传输统计（快照，线程安全）
struct TransportStats
{
    std::uint64_t bytesSent = 0;        // 累计发送字节
    std::uint64_t bytesReceived = 0;    // 累计接收字节
    std::uint64_t packetsSent = 0;      // 累计发送包数
    std::uint64_t packetsReceived = 0;  // 累计接收包数
    std::uint64_t sendErrors = 0;       // 发送错误次数
    std::uint64_t recvErrors = 0;       // 接收错误次数
    std::int64_t lastRecvTimeMs = 0;    // 最近一次收到数据（steady 毫秒）
    std::int64_t lastSendTimeMs = 0;    // 最近一次发送数据（steady 毫秒）
};

class IMediaTransport
{
public:

    virtual ~IMediaTransport() = default;

    // 打开传输通道（解析地址 + 绑定 + 连接）
    virtual bool Open(
        const TransportConfig& config) = 0;

    // 关闭并释放（幂等）
    virtual void Close() = 0;

    // 发送数据（成功返回发送字节数；失败返回 -1）
    virtual int Send(
        const std::uint8_t* data,
        int size) = 0;

    // 接收数据（成功返回接收字节数；超时返回 0；失败返回 -1）
    virtual int Recv(
        std::uint8_t* data,
        int capacity,
        int timeoutMs) = 0;

    // 是否已打开
    virtual bool IsOpen() const = 0;

    // 本地地址（"ip:port"）
    virtual std::string GetLocalAddress() const = 0;

    // 传输统计快照（线程安全）
    virtual TransportStats GetStats() const = 0;
};

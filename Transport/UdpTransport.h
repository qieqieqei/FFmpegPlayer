#pragma once

// ============================================================
// UdpTransport - UDP 传输实现（9.0，评审意见）
//
// 基于 Winsock 的 UDP 传输（connected UDP）：
//   - Open    : WSAStartup（引用计数）-> socket -> bind（可选）
//               -> connect 远端（收敛为 send/recv，无需目标地址）
//   - Send    : send()
//   - Recv    : select() 超时 + recv()（超时返回 0）
//   - Close   : closesocket + WSACleanup（引用计数归零时）
//
// 线程安全：发送/接收可在不同线程调用；统计由互斥锁保护。
//
// 说明：当前 RTSP 路径仍走 FFmpeg NetworkInput，本类是给未来
//       自定义 RTP/RTSP 传输预留的独立模块（不依赖 FFmpeg）。
// ============================================================

#include <cstdint>
#include <mutex>
#include <string>

#include "Transport/IMediaTransport.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

class UdpTransport : public IMediaTransport
{
public:

    UdpTransport();

    ~UdpTransport() override;

    // IMediaTransport
    bool Open(
        const TransportConfig& config) override;

    void Close() override;

    int Send(
        const std::uint8_t* data,
        int size) override;

    int Recv(
        std::uint8_t* data,
        int capacity,
        int timeoutMs) override;

    bool IsOpen() const override;

    std::string GetLocalAddress() const override;

    TransportStats GetStats() const override;

private:

    // Winsock 生命周期（进程级引用计数，见 .cpp）
    static bool EnsureWinsock();

    static void ReleaseWinsock();

#ifdef _WIN32
    SOCKET sock = INVALID_SOCKET;   // 套接字句柄
#endif

    bool isOpen = false;            // 已打开（Open 成功且未 Close）

    std::string localAddress;       // 本地地址（"ip:port"）

    TransportStats stats;           // 传输统计

    mutable std::mutex statsMutex;  // 统计互斥锁
};

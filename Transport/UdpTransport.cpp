// ============================================================
// UdpTransport.cpp - UDP 传输实现（9.0）
//
// Winsock 使用要点：
//   - winsock2.h 必须在 windows.h 之前包含（本文件不包含
//     windows.h，仅 winsock2.h / ws2tcpip.h，避免宏冲突）
//   - WSAStartup / WSACleanup 需要配对的进程级引用计数，
//     由静态计数器 + 互斥锁保证（多实例安全）
// ============================================================

#include "Transport/UdpTransport.h"

#include <chrono>
#include <cstring>

// 平台兼容：非 Windows 编译时提供空实现（当前工程仅 Windows，
// 预留交叉编译出口，避免整文件被条件编译切碎）
#ifndef _WIN32

UdpTransport::UdpTransport()
{
}

UdpTransport::~UdpTransport()
{
    Close();
}

bool UdpTransport::Open(
    const TransportConfig& config)
{
    (void)config;

    return false;
}

void UdpTransport::Close()
{
}

int UdpTransport::Send(
    const std::uint8_t* data,
    int size)
{
    (void)data;
    (void)size;

    return -1;
}

int UdpTransport::Recv(
    std::uint8_t* data,
    int capacity,
    int timeoutMs)
{
    (void)data;
    (void)capacity;
    (void)timeoutMs;

    return -1;
}

bool UdpTransport::IsOpen() const
{
    return false;
}

std::string UdpTransport::GetLocalAddress() const
{
    return std::string();
}

TransportStats UdpTransport::GetStats() const
{
    return TransportStats();
}

bool UdpTransport::EnsureWinsock()
{
    return false;
}

void UdpTransport::ReleaseWinsock()
{
}

#else // _WIN32

namespace
{

// Winsock 进程级引用计数
std::mutex gWinsockMutex;

int gWinsockRefCount = 0;

} // namespace

UdpTransport::UdpTransport()
{
}

UdpTransport::~UdpTransport()
{
    Close();
}

bool UdpTransport::EnsureWinsock()
{
    std::lock_guard<std::mutex> lock(gWinsockMutex);

    if (gWinsockRefCount > 0)
    {
        ++gWinsockRefCount;

        return true;
    }

    WSADATA wsaData;

    int ret =
        WSAStartup(
            MAKEWORD(2, 2),
            &wsaData);

    if (ret != 0)
    {
        return false;
    }

    gWinsockRefCount = 1;

    return true;
}

void UdpTransport::ReleaseWinsock()
{
    std::lock_guard<std::mutex> lock(gWinsockMutex);

    if (gWinsockRefCount > 0)
    {
        --gWinsockRefCount;

        if (gWinsockRefCount == 0)
        {
            WSACleanup();
        }
    }
}

bool UdpTransport::Open(
    const TransportConfig& config)
{
    Close();

    if (!EnsureWinsock())
    {
        return false;
    }

    // ---------- 解析远端地址 ----------

    sockaddr_in remoteAddr;

    std::memset(
        &remoteAddr,
        0,
        sizeof(remoteAddr));

    remoteAddr.sin_family = AF_INET;

    remoteAddr.sin_port =
        htons(
            static_cast<unsigned short>(config.remotePort));

    // inet_pton 失败时回退 InetPtonA（兼容点分十进制；
    // 不用已弃用的 inet_addr）
    if (InetPtonA(
        AF_INET,
        config.remoteHost.c_str(),
        &remoteAddr.sin_addr) != 1)
    {
        ReleaseWinsock();

        return false;
    }

    // ---------- 创建套接字 ----------

    SOCKET s =
        socket(
            AF_INET,
            SOCK_DGRAM,
            IPPROTO_UDP);

    if (s == INVALID_SOCKET)
    {
        ReleaseWinsock();

        return false;
    }

    // ---------- 选项 ----------

    if (config.reuseAddress)
    {
        BOOL reuse = TRUE;

        setsockopt(
            s,
            SOL_SOCKET,
            SO_REUSEADDR,
            reinterpret_cast<const char*>(&reuse),
            sizeof(reuse));
    }

    if (config.recvBufferSize > 0)
    {
        setsockopt(
            s,
            SOL_SOCKET,
            SO_RCVBUF,
            reinterpret_cast<const char*>(
                &config.recvBufferSize),
            sizeof(config.recvBufferSize));
    }

    if (config.sendBufferSize > 0)
    {
        setsockopt(
            s,
            SOL_SOCKET,
            SO_SNDBUF,
            reinterpret_cast<const char*>(
                &config.sendBufferSize),
            sizeof(config.sendBufferSize));
    }

    // ---------- 本地绑定（可选） ----------

    if (config.localPort > 0)
    {
        sockaddr_in localAddr;

        std::memset(
            &localAddr,
            0,
            sizeof(localAddr));

        localAddr.sin_family = AF_INET;

        localAddr.sin_addr.s_addr = htonl(INADDR_ANY);

        localAddr.sin_port =
            htons(
                static_cast<unsigned short>(
                    config.localPort));

        if (bind(
            s,
            reinterpret_cast<sockaddr*>(&localAddr),
            sizeof(localAddr)) == SOCKET_ERROR)
        {
            closesocket(s);

            ReleaseWinsock();

            return false;
        }
    }

    // ---------- 连接远端（connected UDP） ----------

    if (connect(
        s,
        reinterpret_cast<sockaddr*>(&remoteAddr),
        sizeof(remoteAddr)) == SOCKET_ERROR)
    {
        closesocket(s);

        ReleaseWinsock();

        return false;
    }

    // ---------- 本地地址（日志用） ----------

    sockaddr_in local;

    int localLen = sizeof(local);

    char ipBuf[INET_ADDRSTRLEN] = { 0 };

    if (getsockname(
        s,
        reinterpret_cast<sockaddr*>(&local),
        &localLen) == 0)
    {
        inet_ntop(
            AF_INET,
            &local.sin_addr,
            ipBuf,
            sizeof(ipBuf));

        localAddress =
            std::string(ipBuf) +
            ":" +
            std::to_string(ntohs(local.sin_port));
    }
    else
    {
        localAddress.clear();
    }

    sock = s;

    isOpen = true;

    return true;
}

void UdpTransport::Close()
{
#ifdef _WIN32
    if (sock != INVALID_SOCKET)
    {
        closesocket(sock);

        sock = INVALID_SOCKET;
    }
#endif

    if (isOpen)
    {
        isOpen = false;

        ReleaseWinsock();
    }

    localAddress.clear();
}

int UdpTransport::Send(
    const std::uint8_t* data,
    int size)
{
#ifdef _WIN32
    if (!isOpen ||
        sock == INVALID_SOCKET ||
        !data ||
        size <= 0)
    {
        return -1;
    }

    int ret =
        send(
            sock,
            reinterpret_cast<const char*>(data),
            size,
            0);

    if (ret == SOCKET_ERROR)
    {
        std::lock_guard<std::mutex> lock(statsMutex);

        ++stats.sendErrors;

        return -1;
    }

    {
        std::lock_guard<std::mutex> lock(statsMutex);

        stats.bytesSent +=
            static_cast<std::uint64_t>(ret);

        ++stats.packetsSent;

        stats.lastSendTimeMs =
            static_cast<std::int64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
    }

    return ret;
#else
    (void)data;
    (void)size;

    return -1;
#endif
}

int UdpTransport::Recv(
    std::uint8_t* data,
    int capacity,
    int timeoutMs)
{
#ifdef _WIN32
    if (!isOpen ||
        sock == INVALID_SOCKET ||
        !data ||
        capacity <= 0)
    {
        return -1;
    }

    // ---------- select 超时等待 ----------

    fd_set readSet;

    FD_ZERO(&readSet);

    FD_SET(sock, &readSet);

    timeval tv;

    tv.tv_sec =
        timeoutMs / 1000;

    tv.tv_usec =
        (timeoutMs % 1000) * 1000;

    int sel =
        select(
            0,
            &readSet,
            nullptr,
            nullptr,
            &tv);

    if (sel == SOCKET_ERROR)
    {
        std::lock_guard<std::mutex> lock(statsMutex);

        ++stats.recvErrors;

        return -1;
    }

    if (sel == 0)
    {
        // 超时：无数据
        return 0;
    }

    // ---------- 接收 ----------

    int ret =
        recv(
            sock,
            reinterpret_cast<char*>(data),
            capacity,
            0);

    if (ret == SOCKET_ERROR)
    {
        std::lock_guard<std::mutex> lock(statsMutex);

        ++stats.recvErrors;

        return -1;
    }

    if (ret == 0)
    {
        // UDP 不会优雅关闭，0 视为异常
        std::lock_guard<std::mutex> lock(statsMutex);

        ++stats.recvErrors;

        return -1;
    }

    {
        std::lock_guard<std::mutex> lock(statsMutex);

        stats.bytesReceived +=
            static_cast<std::uint64_t>(ret);

        ++stats.packetsReceived;

        stats.lastRecvTimeMs =
            static_cast<std::int64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
    }

    return ret;
#else
    (void)data;
    (void)capacity;
    (void)timeoutMs;

    return -1;
#endif
}

bool UdpTransport::IsOpen() const
{
    return isOpen;
}

std::string UdpTransport::GetLocalAddress() const
{
    return localAddress;
}

TransportStats UdpTransport::GetStats() const
{
    std::lock_guard<std::mutex> lock(statsMutex);

    return stats;
}

#endif // _WIN32

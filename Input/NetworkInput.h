#pragma once

// ============================================================
// NetworkInput - 网络输入（7.1）
//
// 继承 InputSource，负责网络协议：
//   rtsp://   实时流协议（摄像头）
//   rtmp://   直播推流/拉流
//   http(s):// HTTP 点播 / HLS 直播（.m3u8）
//
// 内部调用 avformat_open_input()，按协议注入低延迟选项：
//   - rtsp_transport=tcp   （RTSP 走 TCP，避免 UDP 丢包花屏）
//   - stimeout / rw_timeout（连接与读超时，单位微秒）
//   - fflags=nobuffer      （低延迟：不预读缓冲）
//   - analyzeduration=0 / probesize（快速起播）
//
// 属性：
//   IsLive()     实时流（RTSP/RTMP/直播 HLS）-> 不可 Seek
//   IsSeekable() 点播流（时长已知）-> 可 Seek
//
// 配合 RTSPClient 使用：
//   RTSPClient 负责连接管理 / 超时 / 重连策略，
//   NetworkInput 只负责"打开一条网络输入"。
// ============================================================

#include "Input/InputSource.h"

#include "Config/StreamConfig.h"

class NetworkInput : public InputSource
{
public:

    NetworkInput();

    ~NetworkInput() override;

    // 打开网络流（按协议注入低延迟 / 超时选项）
    bool Open(
        const std::string& url) override;

    // 关闭并释放
    void Close() override;

    // 断线重连：关闭后重新打开同一 URL
    bool Reconnect() override;

    // ---------- 属性 ----------

    bool IsNetwork() const override;

    bool IsReconnectable() const override;

    // 累计重连次数（统计用）
    int GetReconnectCount() const;

    // 设置网络参数（rtsp_transport / 超时 / 低延迟等）
    // 必须在 Open 之前调用；不调用则使用内置默认值
    void SetConfig(
        const StreamConfig& config);

private:

    // 按协议构建 avformat_open_input 的选项字典
    void BuildOptions(
        AVDictionary** opts) const;

    StreamConfig cfg;          // 网络参数

    int reconnectCount = 0;    // 重连次数
};

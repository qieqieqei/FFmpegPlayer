#pragma once

// ============================================================
// CameraInput - 摄像头输入（8.5）
//
// 专门管理 RTSP 摄像头输入源，继承 InputSource：
//
//   1. RTSP 地址      ：Open(url) 打开摄像头流
//   2. 摄像头参数     ：rtsp_transport（tcp/udp）、超时、探测窗口
//   3. 延迟配置       ：SetLatencyMs()；直播不允许像 MP4 那样预缓冲
//
// 与 NetworkInput 的区别：
//   NetworkInput  通用网络流（RTSP/RTMP/HTTP/HLS）
//   CameraInput   RTSP 摄像头专用：默认极限低延迟（max_delay=0）、
//                 fflags=nobuffer + flags=low_delay + rtsp_transport=tcp
//                 三件套低延迟参数、恒为直播（不可 Seek）
//
// 结构（工厂 InputSource::Create 按协议路由）：
//
//                    InputSource
//                        |
//        +---------------+----------------+
//        |               |                |
//   FileInput       NetworkInput     CameraInput
//  （本地文件）   （RTSP/RTMP/HTTP）  （RTSP 摄像头）
//
// 示例：
//   CameraInput cam;
//   cam.SetConfig(cfg);          // stream.json 参数（可选）
//   cam.SetLatencyMs(80);        // 80ms 目标延迟（0 = 极限低延迟，默认）
//   cam.SetTransport("tcp");     // tcp / udp（默认 tcp）
//   cam.Open("rtsp://192.168.1.64:554/ch0");
// ============================================================

#include "Input/InputSource.h"
#include "Config/StreamConfig.h"

class CameraInput : public InputSource
{
public:

    CameraInput();

    ~CameraInput() override;

    // 打开摄像头流（rtsp:// 地址）
    // 低延迟参数（fflags=nobuffer / flags=low_delay / rtsp_transport=tcp）
    // 在内部 BuildOptions 注入 avformat_open_input 的字典
    bool Open(
        const std::string& url) override;

    void Close() override;

    // 断线重连（Player 8.3 断流检测后调用）
    bool Reconnect() override;

    // ---------- 属性 ----------

    bool IsNetwork() const override;

    bool IsReconnectable() const override;

    // 摄像头永远实时：不可 Seek、不允许大缓冲
    bool IsLive() const override;

    bool IsSeekable() const override;

    // ---------- 摄像头参数（Open 前调用，不调用用默认值） ----------

    // 应用 stream.json 配置（工厂 Create 时自动调用）
    void SetConfig(
        const StreamConfig& config);

    // 延迟配置（毫秒）：
    //   > 0 : max_delay = latencyMs 毫秒（RTSP 抖动缓冲上限）
    //   = 0 : max_delay = 0，极限低延迟（默认）
    void SetLatencyMs(
        int latencyMs);

    // 传输协议：tcp（默认）/ udp
    void SetTransport(
        const std::string& transport);

    // 重连次数（统计用）
    int GetReconnectCount() const;

private:

    // 构建 avformat_open_input 的选项字典（低延迟三件套 + 超时 + 探测）
    void BuildOptions(
        AVDictionary** opts) const;

    StreamConfig cfg;           // stream.json 网络参数

    int latencyMs = 0;          // 目标延迟（0 = 极限低延迟）

    int reconnectCount = 0;     // 累计重连次数
};

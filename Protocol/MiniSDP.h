#pragma once

// ============================================================
// MiniSDP - 极简 SDP 解析 / 生成（9.0，评审意见）
//
// 职责：为自定义 RTSP 路径提供最小可用的 SDP 处理：
//   - 解析：会话级（o / s / c / t / a=control）与媒体级
//           （m / a=rtpmap / a=fmtp / a=control / i）
//   - 生成：单媒体 / 多媒体会话文本（RTSP 应答、推流描述用）
//
// 范围：仅覆盖 H.264 / H.265 / AAC / PCMU 等常见直播媒体所需
// 字段；不支持 RTP 加密、群组、带宽等扩展行（忽略即可）。
// ============================================================

#include <string>
#include <vector>

// 单个媒体描述
struct MiniSdpMedia
{
    std::string type;           // "video" / "audio"
    int port = 0;               // 媒体端口（RTP）
    int rtcpPort = 0;           // RTCP 端口（m= 行第二个端口，0 = 未指定）
    std::string proto;          // 传输协议（"RTP/AVP"）
    int payloadType = -1;       // 负载类型（rtpmap 编号）
    std::string encodingName;   // 编码名（"H264" / "MPEG4-GENERIC" / "PCMU"）
    int clockRate = 0;          // 时钟频率（视频 90000 / 音频 48000）
    int channels = 0;           // 音频声道数（0 = 未指定）
    std::string fmtp;           // fmtp 参数字符串（不含 "a=fmtp:<pt> " 前缀）
    std::string control;        // a=control（trackID 或 URL）
    std::string mediaTitle;     // i= 行
};

// 会话描述
struct MiniSdpSession
{
    std::string origin;                 // o= 行原文
    std::string sessionName;            // s= 行
    std::string connectionAddress;      // c= 行（会话级地址）
    std::string sessionControl;         // a=control（会话级）
    std::vector<MiniSdpMedia> medias;   // 媒体列表
};

class MiniSDP
{
public:

    // 解析 SDP 文本。成功返回 true；空文本 / 无媒体行返回 false。
    bool Parse(
        const std::string& text);

    // 查找指定类型媒体（"video" / "audio"），找不到返回 nullptr
    const MiniSdpMedia* FindMedia(
        const std::string& type) const;

    // 生成 SDP 文本
    static std::string Build(
        const MiniSdpSession& session);

    // 便捷：生成单媒体会话文本（RTSP 应答等）
    static std::string BuildMedia(
        const std::string& type,
        int payloadType,
        const std::string& encodingName,
        int clockRate,
        int channels,
        const std::string& fmtp,
        const std::string& control,
        int port,
        const std::string& address);

private:

    // 解析一行 a=rtpmap（失败返回 false）
    static bool ParseRtpmap(
        const std::string& line,
        MiniSdpMedia& media);

    // 解析一行 a=fmtp（失败返回 false）
    static bool ParseFmtp(
        const std::string& line,
        MiniSdpMedia& media);

    // 去掉行尾 \r（SDP 规范行以 \r\n 结束）
    static std::string TrimCr(
        const std::string& line);

    MiniSdpSession session;
};

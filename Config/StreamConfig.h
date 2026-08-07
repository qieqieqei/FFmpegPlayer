#pragma once

// ============================================================
// StreamConfig - 流媒体配置（7.11）
//
// 对应 stream.json，集中管理网络播放 / 推流 / 编码参数：
//
//   {
//     "rtmp_url"              : "rtmp://localhost/live/test",
//     "video_codec"           : "libx264",
//     "audio_codec"           : "aac",
//     "bitrate_kbps"          : 4000,
//     "rtsp_transport"        : "tcp",
//     "rtsp_timeout_ms"       : 5000,
//     "network_timeout_ms"    : 5000,
//     "low_latency"           : true,
//     "reconnect_max_attempts": 3,
//     "reconnect_delay_ms"    : 2000,
//     "max_buffer_packets"    : 600,
//     "buffer_target_ms"      : 300,
//     "hls_segment_duration_sec" : 4,
//     "hls_list_size"         : 6,
//     "video_filter"          : "",
//     "audio_filter"          : ""
//   }
//
// 本阶段（7.1/7.2/7.3）使用的字段：
//   - rtspTransport / timeout / lowLatency / maxBufferPackets / bufferTargetMs
// 后续阶段（编码 / 推流 / HLS）使用其余字段。
// ============================================================

#include <string>

struct StreamConfig
{
    // ---------- 推流（7.6） ----------

    std::string rtmpUrl;                    // RTMP 服务器地址

    // ---------- 编码（7.4） ----------

    std::string videoCodec = "libx264";     // libx264 / libx265 / h264_nvenc

    std::string audioCodec = "aac";         // aac / libopus

    int bitrateKbps = 4000;                 // 视频码率（kbps）

    // ---------- 网络输入（7.1 / 7.2） ----------

    std::string rtspTransport = "tcp";      // rtsp_transport：tcp / udp

    int rtspTimeoutMs = 5000;               // RTSP 连接超时（毫秒）

    int networkTimeoutMs = 5000;            // 通用网络读超时（毫秒）

    bool lowLatency = true;                 // 低延迟模式（nobuffer + 快速探测）

    // ---------- 断线重连（7.2） ----------

    int reconnectMaxAttempts = 3;           // 最大重连次数

    int reconnectDelayMs = 2000;            // 重连间隔（毫秒）

    // ---------- 网络缓冲（7.3） ----------

    int maxBufferPackets = 600;             // 网络缓冲最大包数

    int bufferTargetMs = 300;               // 直播目标缓冲时长（毫秒）

    // ---------- 硬件解码（7.7） ----------

    bool hardwareDecode = true;             // 优先硬件解码（自动回退软解）

    // ---------- HLS 切片（7.7） ----------

    int hlsSegmentDurationSec = 4;          // 单个分片时长（秒）

    int hlsListSize = 6;                    // playlist 保留分片数

    // ---------- 滤镜（7.8） ----------

    std::string videoFilter;                // 例如 "scale=1280:720,drawtext=..."

    std::string audioFilter;                // 例如 "volume=2.0"
};

#pragma once

// ============================================================
// RTMPPublisher - RTMP 推流器（7.5）
//
// 架构：组合 FLVMuxer。
//   RTMP 推流 = FLV 封装 + rtmp 协议（FFmpeg 原生支持），
//   FLVMuxer 已实现"打开输出 / 写包 / 关闭"全部封装逻辑，
//   RTMPPublisher 只增加推流语义：
//     - 连接 / 断开（rtmp:// URL）
//     - 推送状态（已连接 / 已断开）
//     - 断线重连
//     - 从关键帧开始推送（接收端立即起播）
//
// 用法：
//   RTMPPublisher pub;
//   pub.SetConfig(streamConfig);       // rtmp_url / 码率等
//   pub.Connect();
//   pub.AddVideoStream(videoPar, {1,30});
//   pub.AddAudioStream(audioPar);
//   pub.Start();                       // 写头（关键帧起播）
//   pub.PushPacket(pkt);               // 推编码包
//   pub.Stop();                        // 写尾
//   pub.Disconnect();
//
// 限制：RTMP 只支持 H.264 + AAC（FLV 格式限制），
//       编码器必须配套（libx264/h264_nvenc + aac）。
// ============================================================

#include <string>

#include "Muxer/FLVMuxer.h"
#include "Config/StreamConfig.h"

class RTMPPublisher
{
public:

    RTMPPublisher();

    ~RTMPPublisher();

    // 设置推流参数（rtmp_url 等），连接前调用
    void SetConfig(
        const StreamConfig& config);

    // 连接 RTMP 服务器（rtmp://ip:port/app/stream）
    bool Connect(
        const std::string& url);

    // 添加视频流（编码器上下文参数）
    bool AddVideoStream(
        AVCodecParameters* codecpar,
        AVRational streamTimeBase);

    // 添加音频流（编码器上下文参数）
    bool AddAudioStream(
        AVCodecParameters* codecpar);

    // 同步最新编码器参数（第一帧后 extradata 才有效）
    bool RefreshVideoExtradata(
        AVCodecContext* ctx);

    // 开始推流（写 FLV 头；关键帧到达后真正开始推送）
    bool Start();

    // 推送一个编码包
    // 返回 true = 已接受；false = 推送失败（网络中断等）
    bool PushPacket(
        AVPacket* pkt);

    // 停止推流（写尾 + 断开）
    void Stop();

    // 断开连接
    void Disconnect();

    // 断线重连（超过最大次数返回 false）
    bool Reconnect();

    // ---------- 状态查询 ----------

    bool IsConnected() const;

    // 已推的包数 / 字节数（统计）
    int64_t GetPushedPackets() const;

    int64_t GetPushedBytes() const;

    // 连续失败次数（统计）
    int GetFailCount() const;

private:

    FLVMuxer muxer;              // FLV 封装（组合）

    StreamConfig cfg;            // 推流参数

    std::string url;             // RTMP 地址

    bool connected = false;      // 连接状态

    int failCount = 0;           // 连续失败次数

    int64_t pushedPackets = 0;   // 已推包数

    int64_t pushedBytes = 0;     // 已推字节数
};

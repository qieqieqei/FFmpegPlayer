#pragma once

// ============================================================
// MediaContext - 一次媒体会话的管线对象集合 + 媒体派生状态（纯聚合，无逻辑）
//
// 阶段4：把 Player（上帝对象）中"随媒体生死"的成员拆出来。
// 随 OpenMedia / ReleaseMedia 重建；由 Player 持有（unique_ptr<MediaContext>）。
//
// 分组依据（实证，规则3）：以 ReleaseMedia() 的重置/清空边界为准——
//   在 ReleaseMedia() 里被 reset()/Clear()/复位的对象 => 属于 MediaContext；
//   在 Init() 创建、Close() 重置的对象（syncController/seekController）=> 留在 Player；
//   未在该边界复位的会话级状态（currentMediaPath / last*Dropped）=> 留在 Player。
//
// 本结构只承载数据，不承担行为——行为仍留在 Player / 后续 PlaybackSession。
// ============================================================

#include <memory>

#include "pipeline/demux/Demuxer.h"
#include "pipeline/video/VideoDecoder.h"
#include "hardware/HardwareDecoder.h"
#include "pipeline/audio/AudioDecoder.h"
#include "pipeline/audio/AudioResampler.h"
#include "pipeline/audio/SpeedController.h"
#include "output/audio/AudioDevice.h"
#include "pipeline/queue/PacketQueue.h"
#include "pipeline/queue/FrameQueue.h"
#include "streaming/NetworkBuffer.h"
#include "infra/FFmpegPtr.h"
#include "output/video/VideoPresenter.h"

struct MediaContext
{
    // ---------- 解复用 / 视频解码 ----------

    // 解复用器（Demux 线程）
    std::unique_ptr<Demuxer> demuxer;

    // 视频解码器（Video 线程）
    std::unique_ptr<VideoDecoder> videoDecoder;

    // 硬件视频解码器（Video 线程；激活时优先于 videoDecoder）
    std::unique_ptr<HardwareDecoder> hwDecoder;

    // 硬件帧 -> 系统内存的拷贝目标（Video 线程，复用）
    AVFramePtr hwTransferFrame;

    // ---------- 音频链路 ----------

    // 音频解码器（Audio 线程）
    std::unique_ptr<AudioDecoder> audioDecoder;

    // 音频重采样器（Audio 线程）
    std::unique_ptr<AudioResampler> audioResampler;

    // 变速不变调（Audio 线程使用，SetSpeed 跨线程）
    std::unique_ptr<SpeedController> speedController;

    // SDL 音频设备（回调线程 + Audio 线程）
    std::unique_ptr<AudioDevice> audioDevice;

    // ---------- 队列（地址稳定：MediaContext 本身只建一次） ----------

    // 视频包队列（点播：满阻塞背压）
    PacketQueue videoPacketQueue;

    // 音频包队列（点播：满阻塞背压；8.5 直播：LiveMode 追最新，丢旧包）
    PacketQueue audioPacketQueue;

    // 视频包队列（直播：满丢最旧 + 时长上限，低延迟 7.3/8.5）
    NetworkBuffer videoNetBuffer;

    // 直播流：Demux<->Decode 走 NetworkBuffer
    bool useNetBuffer = false;

    // 视频帧队列（Video -> Render）
    FrameQueue videoFrameQueue;

    // ---------- 媒体派生状态 ----------

    // 媒体总时长（秒）
    double duration = 0.0;

    // 视频帧时长（无音频时按它匀速播放）
    double videoFrameDuration = 1.0 / 25.0;

    // 是否存在可用音频流
    bool hasAudioStream = false;

    // ---------- presentation (phase 5.2) ----------

    // SDL window / renderer / textures + YUV->RGB sws + RGB buffer + last frame;
    // created in PlaybackSession::OpenMedia, released in ReleaseMedia.
    VideoPresenter presenter;
};

#pragma once

// ============================================================
// MediaContext - 一次媒体会话的管线对象集合（纯聚合，无逻辑）
//
// 阶段4：把 Player（上帝对象）中"随媒体生死"的成员拆出来。
// 随 OpenMedia / ReleaseMedia 重建；由 Player 持有（unique_ptr<MediaContext>）。
//
// 本结构只承载对象，不承担行为——行为仍留在 Player/后续 PlaybackSession。
// ============================================================

#include <memory>

#include "pipeline/demux/Demuxer.h"
#include "pipeline/video/VideoDecoder.h"
#include "hardware/HardwareDecoder.h"
#include "infra/FFmpegPtr.h"

struct MediaContext
{
    // 解复用器（Demux 线程）
    std::unique_ptr<Demuxer> demuxer;

    // 视频解码器（Video 线程）
    std::unique_ptr<VideoDecoder> videoDecoder;

    // 硬件视频解码器（Video 线程；激活时优先于 videoDecoder）
    std::unique_ptr<HardwareDecoder> hwDecoder;

    // 硬件帧 -> 系统内存的拷贝目标（Video 线程，复用）
    AVFramePtr hwTransferFrame;
};

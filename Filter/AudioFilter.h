#pragma once

// ============================================================
// AudioFilter - 音频滤镜（7.8）
//
// FilterGraph 的音频专用薄封装，负责携带音频参数。
//
// 用法：
//   AudioFilter filter;
//   filter.Init("volume=2.0", AV_SAMPLE_FMT_S16, 48000,
//               AV_CH_LAYOUT_STEREO);
//   AVFrame* out = nullptr;
//   filter.Process(frame, &out);
//
// 常用滤镜示例：
//   "volume=2.0"           音量放大 2 倍
//   "highpass=f=200"       高通滤波（去低频噪声）
//   "lowpass=f=8000"       低通滤波
//   "atempo=1.5"           变速（不变调）
//   "anull"                直通（无效果）
//
// 注意：
//   - 输入帧格式必须与 Init 时一致（采样率 / 格式 / 声道）
//   - atempo 输入输出采样率不变，但需要整数帧，注意缓冲
// ============================================================

#include <string>

#include "Filter/FilterGraph.h"

class AudioFilter
{
public:

    AudioFilter();

    ~AudioFilter();

    // 初始化音频滤镜
    // filterDesc : 滤镜描述，如 "volume=2.0"
    bool Init(
        const std::string& filterDesc,
        AVSampleFormat sampleFmt,
        int sampleRate,
        uint64_t channelLayout);

    // 处理一帧（借用内部帧，用完 av_frame_unref）
    bool Process(
        AVFrame* in,
        AVFrame** out);

    // 关闭并释放
    void Close();

    // 是否已初始化
    bool IsReady() const;

private:

    FilterGraph graph;
};

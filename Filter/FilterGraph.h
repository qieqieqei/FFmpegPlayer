#pragma once

// ============================================================
// FilterGraph - 滤镜图（7.8 / 7.7）
//
// 通用 avfilter 封装，支持视频滤镜与音频滤镜：
//
//   输入帧 --> [buffer] --> [滤镜链] --> [buffersink] --> 输出帧
//
// 示例滤镜描述：
//   视频： "scale=1280:720" / "hflip" / "drawtext=...:text='Hello'"
//   音频： "volume=2.0" / "highpass=f=200"
//
// 用法（视频）：
//   FilterGraph g;
//   g.InitVideo("scale=1280:720", 1920, 1080,
//               AV_PIX_FMT_YUV420P, {1,30}, {30,1});
//   AVFrame* out = nullptr;
//   g.ProcessFrame(frame, &out);   // out 借用内部帧，用完 av_frame_unref
//
// 注意：
//   - ProcessFrame 可能没有输出（滤镜缓冲 / 多入一出），返回 false
//   - 输出帧复用 FilterGraph 内部 AVFrame，调用方用完 av_frame_unref
//     （内部在下一次 ProcessFrame 前自动 unref）
// ============================================================

#include <string>

#include "Utils/FFmpegPtr.h"

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/rational.h>
#include <libavutil/samplefmt.h>
#include <libavutil/pixfmt.h>
}

class FilterGraph
{
public:

    FilterGraph();

    ~FilterGraph();

    // 初始化视频滤镜链
    // filterDesc : 滤镜描述，如 "scale=1280:720,hflip"
    // 其余参数为输入视频格式（来自解码器/编码器上下文）
    bool InitVideo(
        const std::string& filterDesc,
        int width,
        int height,
        AVPixelFormat pixFmt,
        AVRational timeBase,
        AVRational frameRate);

    // 初始化音频滤镜链
    // filterDesc : 滤镜描述，如 "volume=2.0"
    bool InitAudio(
        const std::string& filterDesc,
        AVSampleFormat sampleFmt,
        int sampleRate,
        uint64_t channelLayout);

    // 处理一帧。
    // 成功输出时返回 true 并设置 out（借用内部帧，
    // 调用方用完 av_frame_unref；下一次调用会自动清空）。
    // 无输出（滤镜缓冲）或失败返回 false。
    bool ProcessFrame(
        AVFrame* in,
        AVFrame** out);

    // 冲刷滤镜链尾帧（结束处理时调用，把缓冲帧全部输出）
    // 之后反复 ProcessFrame(nullptr, &out) 直到返回 false
    void Flush();

    // 关闭并释放
    void Close();

    // 是否已初始化
    bool IsReady() const;

    // 输出视频格式（InitVideo 后查询）
    int GetOutputWidth() const;

    int GetOutputHeight() const;

private:

    // 内部：把 in 送入 buffersrc
    bool SendInput(
        AVFrame* in);

    // uint64 -> 十六进制字符串（channel_layout 参数）
    static std::string ToHex(
        uint64_t v);

    AVFilterGraphPtr graph;        // 滤镜图（RAII，avfilter_graph_free）

    AVFilterContext* srcCtx = nullptr;  // buffer 源（属于 graph，借用）

    AVFilterContext* sinkCtx = nullptr; // buffersink 汇（属于 graph，借用）

    AVFramePtr outFrame;          // 内部输出帧（复用，RAII）

    bool ready = false;               // 初始化标志

    bool isVideo = false;             // 视频 / 音频

    int outWidth = 0;                 // 输出宽

    int outHeight = 0;                // 输出高
};

#pragma once

// ============================================================
// VideoFilter - 视频滤镜（7.8）
//
// FilterGraph 的视频专用薄封装，负责携带视频参数。
//
// 用法：
//   VideoFilter filter;
//   filter.Init("scale=1280:720", 1920, 1080,
//               AV_PIX_FMT_YUV420P, {1,30}, {30,1});
//   AVFrame* out = nullptr;
//   filter.Process(frame, &out);
//
// 常用滤镜示例：
//   "scale=1280:720"          缩放
//   "hflip" / "vflip"         水平 / 垂直翻转
//   "rotate=90*PI/180"        旋转
//   "drawtext=fontfile=simhei.ttf:text='Camera 1':fontsize=36"
//                             叠加文字（水印 / 时间戳）
//   "fps=30"                  帧率转换
// ============================================================

#include <string>

#include "Filter/FilterGraph.h"

class VideoFilter
{
public:

    VideoFilter();

    ~VideoFilter();

    // 初始化视频滤镜
    // filterDesc : 滤镜描述，如 "scale=1280:720,drawtext=..."
    // 其余参数为输入视频格式（通常来自解码器上下文）
    bool Init(
        const std::string& filterDesc,
        int width,
        int height,
        AVPixelFormat pixFmt,
        AVRational timeBase,
        AVRational frameRate);

    // 处理一帧（借用内部帧，用完 av_frame_unref）
    bool Process(
        AVFrame* in,
        AVFrame** out);

    // 关闭并释放
    void Close();

    // 是否已初始化
    bool IsReady() const;

    // 输出尺寸（可能被滤镜改变）
    int GetOutputWidth() const;

    int GetOutputHeight() const;

private:

    FilterGraph graph;
};

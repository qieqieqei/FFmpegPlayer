#include "Filter/VideoFilter.h"

// ============================================================
// VideoFilter - 视频滤镜
// ============================================================

VideoFilter::VideoFilter()
{
}

VideoFilter::~VideoFilter()
{
    Close();
}

bool VideoFilter::Init(
    const std::string& filterDesc,
    int width,
    int height,
    AVPixelFormat pixFmt,
    AVRational timeBase,
    AVRational frameRate)
{
    return
        graph.InitVideo(
            filterDesc,
            width,
            height,
            pixFmt,
            timeBase,
            frameRate);
}

bool VideoFilter::Process(
    AVFrame* in,
    AVFrame** out)
{
    return
        graph.ProcessFrame(
            in,
            out);
}

void VideoFilter::Close()
{
    graph.Close();
}

bool VideoFilter::IsReady() const
{
    return graph.IsReady();
}

int VideoFilter::GetOutputWidth() const
{
    return graph.GetOutputWidth();
}

int VideoFilter::GetOutputHeight() const
{
    return graph.GetOutputHeight();
}

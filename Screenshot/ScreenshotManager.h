#pragma once

// ============================================================
// ScreenshotManager - 截图系统升级（5.6）
//
// 支持：
//   - PNG / JPG 格式
//   - 时间命名：screenshot_HH_MM_SS_mmm.png
//   - 同秒自动编号：_001 _002 ...
//
// 编码方式：
//   AVFrame(YUV) --sws_scale--> RGB24 --avcodec--> PNG/JPG 文件
//
// 用法：
//   screenshotManager->SaveFrame(frame);           // 默认 PNG
//   screenshotManager->SaveFrame(frame, "jpg");    // JPG
// ============================================================

#include <string>
#include <mutex>

#include "Utils/FFmpegPtr.h"

extern "C" {
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

class ScreenshotManager
{
public:

    ScreenshotManager();

    ~ScreenshotManager();

    // 保存一帧截图（默认 PNG）
    bool SaveFrame(
        AVFrame* frame);

    // 保存一帧截图
    // format: "png" 或 "jpg"
    bool SaveFrame(
        AVFrame* frame,
        const std::string& format);

    // 设置输出目录（默认 screenshots）
    void SetOutputDir(
        const std::string& dir);

private:

    // YUV -> RGB24 转换并保存
    bool SaveRGB(
        const uint8_t* rgbData,
        int width,
        int height,
        int linesize,
        const std::string& format);

    // 用 FFmpeg 编码器写文件
    bool EncodeImage(
        const uint8_t* rgbData,
        int width,
        int height,
        int linesize,
        const std::string& format,
        const std::string& filename);

    // 生成文件名：screenshot_HH_MM_SS_mmm[_NNN].ext
    std::string GenerateFilename(
        const std::string& ext);

    // 把 RGB24 数据写进 AVFrame
    void FillRGBFrame(
        AVFrame* rgbFrame,
        const uint8_t* rgbData,
        int width,
        int height,
        int linesize);

    // 编码一帧 -> 写入文件
    bool EncodeAndWrite(
        AVFrame* rgbFrame,
        const std::string& format,
        const std::string& filename);

    std::string outputDir = "screenshots";

    SwsContextPtr sws;          // YUV -> RGB 转换器（RAII，惰性创建）

    std::mutex mutex;               // 串行化截图操作

    int lastSecond = -1;            // 同一秒内自动编号

    int seq = 0;
};

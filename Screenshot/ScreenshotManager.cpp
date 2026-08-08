#include "Screenshot/ScreenshotManager.h"

#include "Utils/ErrorHandler.h"
#include "Utils/Logger.h"
#include "Utils/FFmpegPtr.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

#include <SDL.h>

#include <iostream>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <iomanip>

ScreenshotManager::ScreenshotManager()
{
}

ScreenshotManager::~ScreenshotManager()
{
    // RAII：sws 自动释放
}

bool ScreenshotManager::SaveFrame(
    AVFrame* frame)
{
    return SaveFrame(frame, "png");
}

bool ScreenshotManager::SaveFrame(
    AVFrame* frame,
    const std::string& format)
{
    if (!frame)
    {
        ErrorHandler::Log(
            ErrorTag::Screenshot,
            "SaveFrame : frame is null");

        return false;
    }

    std::lock_guard<std::mutex> lock(mutex);

    // 惰性创建 sws 转换器
    if (!sws)
    {
        sws.reset(
            sws_getContext(
                frame->width,
                frame->height,
                static_cast<AVPixelFormat>(frame->format),
                frame->width,
                frame->height,
                AV_PIX_FMT_RGB24,
                SWS_BILINEAR,
                nullptr,
                nullptr,
                nullptr));

        if (!sws)
        {
            ErrorHandler::Log(
                ErrorTag::Screenshot,
                "sws_getContext failed");

            return false;
        }
    }

    // 转换到 RGB24
    int linesize =
        frame->width * 3;

    std::vector<uint8_t> rgb(
        static_cast<size_t>(linesize) * frame->height);

    uint8_t* dst[1] = { rgb.data() };

    int dstLinesize[1] = { linesize };

    sws_scale(
        sws.get(),
        frame->data,
        frame->linesize,
        0,
        frame->height,
        dst,
        dstLinesize);

    return SaveRGB(
        rgb.data(),
        frame->width,
        frame->height,
        linesize,
        format);
}

void ScreenshotManager::SetOutputDir(
    const std::string& dir)
{
    outputDir = dir;
}

bool ScreenshotManager::SaveRGB(
    const uint8_t* rgbData,
    int width,
    int height,
    int linesize,
    const std::string& format)
{
    std::string ext =
        (format == "jpg" ||
         format == "jpeg") ?
        "jpg" :
        "png";

    std::string filename =
        GenerateFilename(ext);

    return EncodeImage(
        rgbData,
        width,
        height,
        linesize,
        ext,
        filename);
}

std::string ScreenshotManager::GenerateFilename(
    const std::string& ext)
{
    // 当前播放时间（秒）
    Uint32 nowMs = SDL_GetTicks();

    int totalSeconds =
        static_cast<int>(nowMs / 1000);

    int hour =
        totalSeconds / 3600;

    int minute =
        (totalSeconds % 3600) / 60;

    int second =
        totalSeconds % 60;

    int millis =
        static_cast<int>(nowMs % 1000);

    std::ostringstream oss;

    oss
        << "screenshot_"
        << std::setfill('0')
        << std::setw(2)
        << hour
        << "_"
        << std::setw(2)
        << minute
        << "_"
        << std::setw(2)
        << second
        << "_"
        << std::setw(3)
        << millis;

    // 同一秒内自动编号
    if (totalSeconds == lastSecond)
    {
        seq++;

        oss
            << "_"
            << std::setw(3)
            << seq;
    }
    else
    {
        lastSecond = totalSeconds;

        seq = 0;
    }

    oss
        << "."
        << ext;

    return oss.str();
}

void ScreenshotManager::FillRGBFrame(
    AVFrame* rgbFrame,
    const uint8_t* rgbData,
    int width,
    int height,
    int linesize)
{
    av_frame_make_writable(rgbFrame);

    for (int y = 0; y < height; y++)
    {
        std::memcpy(
            rgbFrame->data[0] +
                static_cast<size_t>(y) * rgbFrame->linesize[0],
            rgbData +
                static_cast<size_t>(y) * linesize,
            static_cast<size_t>(width) * 3);
    }
}

bool ScreenshotManager::EncodeImage(
    const uint8_t* rgbData,
    int width,
    int height,
    int linesize,
    const std::string& format,
    const std::string& filename)
{
    // JPG 使用 YUVJ420P（全范围 JPEG 标准）
    // PNG 使用 RGB24
    AVPixelFormat pixFmt =
        (format == "jpg") ?
        AV_PIX_FMT_YUVJ420P :
        AV_PIX_FMT_RGB24;

    // 构造编码输入帧
    AVFramePtr frame(
        av_frame_alloc());

    if (!frame)
    {
        ErrorHandler::Log(
            ErrorTag::Screenshot,
            "av_frame_alloc failed");

        return false;
    }

    frame->width = width;

    frame->height = height;

    frame->format = pixFmt;

    if (av_frame_get_buffer(frame.get(), 32) < 0)
    {
        ErrorHandler::Log(
            ErrorTag::Screenshot,
            "av_frame_get_buffer failed");

        return false;
    }

    if (pixFmt == AV_PIX_FMT_YUVJ420P)
    {
        // YUVJ420P：需要先把 RGB24 转成 YUV
        SwsContextPtr yuvSws(
            sws_getContext(
                width,
                height,
                AV_PIX_FMT_RGB24,
                width,
                height,
                AV_PIX_FMT_YUVJ420P,
                SWS_BILINEAR,
                nullptr,
                nullptr,
                nullptr));

        if (!yuvSws)
        {
            ErrorHandler::Log(
                ErrorTag::Screenshot,
                "sws_getContext (RGB->YUV) failed");

            return false;
        }

        uint8_t* src[1] = {
            const_cast<uint8_t*>(rgbData)
        };

        int srcLinesize[1] = { linesize };

        sws_scale(
            yuvSws.get(),
            src,
            srcLinesize,
            0,
            height,
            frame->data,
            frame->linesize);
    }
    else
    {
        FillRGBFrame(
            frame.get(),
            rgbData,
            width,
            height,
            linesize);
    }

    return EncodeAndWrite(
        frame.get(),
        format,
        filename);
}

bool ScreenshotManager::EncodeAndWrite(
    AVFrame* rgbFrame,
    const std::string& format,
    const std::string& filename)
{
    AVCodecID codecId =
        (format == "jpg") ?
        AV_CODEC_ID_MJPEG :
        AV_CODEC_ID_PNG;

    const AVCodec* codec =
        avcodec_find_encoder(codecId);

    if (!codec)
    {
        return false;
    }

    AVCodecContextPtr ctx(
        avcodec_alloc_context3(codec));

    ctx->width = rgbFrame->width;

    ctx->height = rgbFrame->height;

    ctx->pix_fmt =
        static_cast<AVPixelFormat>(rgbFrame->format);

    ctx->time_base = AVRational{ 1, 25 };

    if (codecId == AV_CODEC_ID_MJPEG)
    {
        ctx->qmin = 2;

        ctx->qmax = 20;

        // FFmpeg 8.x 移除了 AVCodecContext::qscale 字段，改用编码器私有选项 q
        av_opt_set_int(
            ctx.get(),
            "q",
            8,   // 画质
            0);
    }

    if (avcodec_open2(ctx.get(), codec, nullptr) < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Screenshot,
            "avcodec_open2 (" + format + ")",
            -1);

        return false;
    }

    // 创建输出文件
    std::filesystem::create_directories(outputDir);

    std::string path =
        outputDir + "/" + filename;

    FILE* file = nullptr;

    if (fopen_s(&file, path.c_str(), "wb") != 0 ||
        !file)
    {
        ErrorHandler::Log(
            ErrorTag::Screenshot,
            "Cannot open output file : " + path);

        return false;
    }

    bool ok = false;

    AVPacketPtr pkt(
        av_packet_alloc());

    int ret =
        avcodec_send_frame(
            ctx.get(),
            rgbFrame);

    if (ret >= 0)
    {
        ret =
            avcodec_receive_packet(
                ctx.get(),
                pkt.get());

        if (ret >= 0)
        {
            fwrite(
                pkt->data,
                1,
                pkt->size,
                file);

            ok = true;

            Logger::Info()
                << "Screenshot Saved : "
                << path
                << " ("
                << pkt->size
                << " bytes, "
                << format
                << ")"
                << std::endl;
        }
    }

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Screenshot,
            "Encode image",
            ret);
    }

    fclose(file);

    return ok;
}

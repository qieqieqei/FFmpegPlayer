#include "Hardware/HardwareDecoder.h"

#include "Utils/ErrorHandler.h"
#include "Utils/Logger.h"

extern "C" {
#include <libavutil/pixdesc.h>
}

// ============================================================
// HardwareDecoder - 硬件视频解码器
// ============================================================

HardwareDecoder::HardwareDecoder()
{
}

HardwareDecoder::~HardwareDecoder()
{
    // RAII：codecCtx / frame / framesRef 自动释放
}

void HardwareDecoder::SetLowDelay(
    bool enable)
{
    lowDelay = enable;
}

bool HardwareDecoder::Init(
    CUDAContext* cuda,
    const std::string& codecName,
    AVCodecParameters* codecpar)
{
    Close();

    this->cuda = cuda;

    if (!codecpar)
    {
        return false;
    }

    // ---------- 查找解码器（h264 / hevc 软解器 + hwaccel） ----------

    const AVCodec* codec =
        avcodec_find_decoder_by_name(
            codecName.c_str());

    if (!codec)
    {
        ErrorHandler::Log(
            ErrorTag::Decoder,
            "Decoder not found : " +
            codecName);

        return false;
    }

    codecCtx.reset(
        avcodec_alloc_context3(codec));

    if (!codecCtx)
    {
        ErrorHandler::Log(
            ErrorTag::Decoder,
            "avcodec_alloc_context3 failed");

        return false;
    }

    // 用流的编码参数填充（含 extradata——SPS/PPS，
    // 不填的话 h264 硬解报 Invalid data）
    int ret =
        avcodec_parameters_to_context(
            codecCtx.get(),
            codecpar);

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Decoder,
            "avcodec_parameters_to_context (hw)",
            ret);

        return false;
    }

    int width =
        codecCtx->width;

    int height =
        codecCtx->height;

    // 线程数（软解回退时有效）
    codecCtx->thread_count = 4;

    // ---------- 尝试硬件加速 ----------

    hardware =
        cuda && cuda->IsAvailable();

    if (hardware)
    {
        // 绑定硬件设备
        codecCtx->hw_device_ctx =
            av_buffer_ref(
                cuda->GetDeviceContext());

        hwPixFmt =
            CUDAContext::DeviceFormat(
                cuda->GetType());

        // get_format 回调：解码器选硬件像素格式
        codecCtx->get_format =
            OnGetFormat;
    }

    // ---------- 打开解码器 ----------

    // 8.5：低延迟模式（直播）时 avcodec_open2 传 flags=low_delay
    AVDictionary* opts = nullptr;

    if (lowDelay)
    {
        av_dict_set(
            &opts,
            "flags",
            "low_delay",
            0);
    }

    ret =
        avcodec_open2(
            codecCtx.get(),
            codec,
            lowDelay ? &opts : nullptr);

    av_dict_free(&opts);

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Decoder,
            "avcodec_open2 (" + codecName + ")",
            ret);

        hardware = false;

        // 硬件失败：清掉设备上下文，纯软解重开
        av_buffer_unref(
            &codecCtx->hw_device_ctx);

        codecCtx->get_format = nullptr;

        // 8.5：软解回退同样带低延迟选项
        AVDictionary* fallbackOpts = nullptr;

        if (lowDelay)
        {
            av_dict_set(
                &fallbackOpts,
                "flags",
                "low_delay",
                0);
        }

        ret =
            avcodec_open2(
                codecCtx.get(),
                codec,
                lowDelay ? &fallbackOpts : nullptr);

        av_dict_free(&fallbackOpts);

        if (ret < 0)
        {
            ErrorHandler::LogFFmpeg(
                ErrorTag::Decoder,
                "avcodec_open2 fallback software",
                ret);

            Close();

            return false;
        }

        Logger::Warn()
            << "[HardwareDecoder] Fallback to software : "
            << codecName
            << std::endl;
    }

    // ---------- 硬件模式：创建帧池 ----------

    if (hardware)
    {
        framesRef.reset(
            cuda->CreateFramesRef(
                width,
                height));

        if (framesRef)
        {
            codecCtx->hw_frames_ctx =
                av_buffer_ref(
                    framesRef.get());
        }
        else
        {
            // 帧池失败也回退软解
            Logger::Warn()
                << "[HardwareDecoder] hw_frames_ctx failed, "
                << "fallback to software"
                << std::endl;

            hardware = false;
        }
    }

    frame.reset(
        av_frame_alloc());

    if (!frame)
    {
        ErrorHandler::Log(
            ErrorTag::Decoder,
            "av_frame_alloc failed");

        Close();

        return false;
    }

    Logger::Info()
        << "[HardwareDecoder] "
        << codecName
        << " : "
        << (hardware ? cuda->GetTypeName() : "software")
        << " "
        << width
        << "x"
        << height
        << std::endl;

    return true;
}

bool HardwareDecoder::SendPacket(
    AVPacket* pkt)
{
    if (!codecCtx)
    {
        return false;
    }

    int ret =
        avcodec_send_packet(
            codecCtx.get(),
            pkt);

    if (ret < 0 &&
        ret != AVERROR(EAGAIN))
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Decoder,
            "avcodec_send_packet (hw)",
            ret);

        return false;
    }

    return true;
}

AVFrame* HardwareDecoder::ReceiveFrame()
{
    if (!codecCtx || !frame)
    {
        return nullptr;
    }

    av_frame_unref(
        frame.get());

    int ret =
        avcodec_receive_frame(
            codecCtx.get(),
            frame.get());

    if (ret < 0)
    {
        return nullptr;
    }

    return frame.get();
}

bool HardwareDecoder::TransferFrame(
    AVFrame* hwFrame,
    AVFrame* dst)
{
    if (!hwFrame || !dst)
    {
        return false;
    }

    // 软件帧：直接引用拷贝
    if (!hardware)
    {
        av_frame_unref(dst);

        return
            av_frame_ref(dst, hwFrame) >= 0;
    }

    // 硬件帧：从显存拷回系统内存
    av_frame_unref(dst);

    int ret =
        av_hwframe_transfer_data(
            dst,
            hwFrame,
            0);

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Decoder,
            "av_hwframe_transfer_data",
            ret);

        return false;
    }

    // 补上时间信息（transfer 不复制这些字段）
    dst->pts = hwFrame->pts;

    dst->duration = hwFrame->duration;

    dst->time_base = hwFrame->time_base;

    return true;
}

void HardwareDecoder::Flush()
{
    if (!codecCtx)
    {
        return;
    }

    avcodec_flush_buffers(
        codecCtx.get());
}

void HardwareDecoder::Close()
{
    // RAII：reset(nullptr) 立即释放全部资源
    codecCtx.reset();

    frame.reset();

    framesRef.reset();

    cuda = nullptr;

    hwPixFmt = AV_PIX_FMT_NONE;

    hardware = false;
}

bool HardwareDecoder::IsHardware() const
{
    return hardware;
}

bool HardwareDecoder::IsReady() const
{
    return codecCtx.get() != nullptr;
}

AVCodecContext* HardwareDecoder::GetContext() const
{
    return codecCtx.get();
}

AVBufferRef* HardwareDecoder::GetHardwareFramesRef() const
{
    return framesRef.get();
}

// ============================================================
// get_format 回调
// ============================================================

AVPixelFormat HardwareDecoder::OnGetFormat(
    AVCodecContext* ctx,
    const enum AVPixelFormat* pixFmts)
{
    // 从解码器支持的像素格式中找硬件格式
    for (const AVPixelFormat* p = pixFmts; *p != AV_PIX_FMT_NONE; p++)
    {
        if (*p == AV_PIX_FMT_CUDA ||
            *p == AV_PIX_FMT_D3D11 ||
            *p == AV_PIX_FMT_DXVA2_VLD)
        {
            return *p;
        }
    }

    // 没有硬件格式：用第一个（软解）
    return pixFmts[0];
}

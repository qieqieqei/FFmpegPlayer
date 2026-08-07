#include "Hardware/CUDAContext.h"

#include "Utils/ErrorHandler.h"
#include "Utils/Logger.h"

// ============================================================
// CUDAContext - GPU 硬件上下文
// ============================================================

CUDAContext::CUDAContext()
{
}

CUDAContext::~CUDAContext()
{
    Close();
}

bool CUDAContext::Init()
{
    Close();

    // ---------- 按优先级尝试硬件设备 ----------
    // CUDA（NVDEC）> D3D11VA > DXVA2
    // 每个都尝试真实创建，避免仅枚举类型导致假阳性

    const AVHWDeviceType candidates[] =
    {
        AV_HWDEVICE_TYPE_CUDA,
        AV_HWDEVICE_TYPE_D3D11VA,
        AV_HWDEVICE_TYPE_DXVA2
    };

    for (AVHWDeviceType t : candidates)
    {
        AVBufferRef* ref = nullptr;

        // device 传 nullptr = 默认设备（CUDA 默认 0 号卡）
        int ret =
            av_hwdevice_ctx_create(
                &ref,
                t,
                nullptr,
                nullptr,
                0);

        if (ret >= 0 && ref)
        {
            deviceCtx = ref;

            type = t;

            Logger::Info()
                << "[CUDAContext] Hardware device : "
                << av_hwdevice_get_type_name(t)
                << std::endl;

            return true;
        }

        if (ret < 0 &&
            ret != AVERROR(ENOSYS))
        {
            // ENOSYS = 该构建未编译该后端，静默跳过；
            // 其余错误（驱动缺失等）记一条调试日志
            Logger::Warn()
                << "[CUDAContext] "
                << av_hwdevice_get_type_name(t)
                << " init failed"
                << std::endl;
        }
    }

    ErrorHandler::Log(
        ErrorTag::Network,   // 复用 Network 标签（Hardware 未占用）
        "No hardware device available (cuda/d3d11va/dxva2)");

    return false;
}

void CUDAContext::Close()
{
    if (deviceCtx)
    {
        av_buffer_unref(&deviceCtx);
    }

    type = AV_HWDEVICE_TYPE_NONE;
}

bool CUDAContext::IsAvailable() const
{
    return deviceCtx != nullptr;
}

AVHWDeviceType CUDAContext::GetType() const
{
    return type;
}

std::string CUDAContext::GetTypeName() const
{
    if (!deviceCtx)
    {
        return "none";
    }

    const char* name =
        av_hwdevice_get_type_name(type);

    return name ? name : "unknown";
}

AVBufferRef* CUDAContext::GetDeviceContext() const
{
    return deviceCtx;
}

AVBufferRef* CUDAContext::CreateFramesRef(
    int width,
    int height,
    AVPixelFormat swFormat) const
{
    if (!deviceCtx)
    {
        return nullptr;
    }

    AVBufferRef* framesRef =
        av_hwframe_ctx_alloc(deviceCtx);

    if (!framesRef)
    {
        return nullptr;
    }

    AVHWFramesContext* frames =
        reinterpret_cast<AVHWFramesContext*>(
            framesRef->data);

    frames->format =
        DeviceFormat(type);   // 设备对应的像素格式

    frames->sw_format = swFormat;

    frames->width = width;

    frames->height = height;

    frames->initial_pool_size = 20;    // 帧池大小

    int ret =
        av_hwframe_ctx_init(framesRef);

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Network,
            "av_hwframe_ctx_init",
            ret);

        av_buffer_unref(&framesRef);

        return nullptr;
    }

    return framesRef;
}

// 设备类型 -> 帧像素格式
AVPixelFormat CUDAContext::DeviceFormat(
    AVHWDeviceType type)
{
    switch (type)
    {
    case AV_HWDEVICE_TYPE_CUDA:    return AV_PIX_FMT_CUDA;
    case AV_HWDEVICE_TYPE_D3D11VA: return AV_PIX_FMT_D3D11;
    case AV_HWDEVICE_TYPE_DXVA2:   return AV_PIX_FMT_DXVA2_VLD;
    default:                       return AV_PIX_FMT_NONE;
    }
}

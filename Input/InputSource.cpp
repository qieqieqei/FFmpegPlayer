#include "Input/InputSource.h"

#include "Config/ConfigManager.h"
#include "Input/FileInput.h"
#include "Input/NetworkInput.h"
#include "Utils/ErrorHandler.h"

// ============================================================
// InputSource - 统一输入接口（基类实现）
// ============================================================

InputSource::InputSource()
{
}

InputSource::~InputSource()
{
    Close();
}

void InputSource::Close()
{
    // 基类默认空实现（派生类负责释放 fmt）
    // 注意：派生类析构时先执行自己的 Close，
    //       再执行基类析构（此时虚调用落到本空实现）
}

bool InputSource::Reconnect()
{
    // 默认不支持重连（文件流）
    return false;
}

bool InputSource::IsNetwork() const
{
    return false;
}

bool InputSource::IsReconnectable() const
{
    return false;
}

bool InputSource::IsLive() const
{
    return live;
}

bool InputSource::IsSeekable() const
{
    return seekable;
}

const std::string& InputSource::GetProtocol() const
{
    return protocol;
}

const std::string& InputSource::GetUrl() const
{
    return url;
}

void InputSource::SetAbort(
    bool abort)
{
    this->abort.store(abort);
}

AVFormatContext* InputSource::GetFormatContext() const
{
    return fmt;
}

// ============================================================
// 工厂：根据 URL 协议创建输入源
// ============================================================

InputSource* InputSource::Create(
    const std::string& url,
    const StreamConfig* cfg)
{
    if (ConfigManager::IsNetworkUrl(url))
    {
        // 网络流：NetworkInput（内部包含 RTSP/RTMP/HTTP/HLS 支持）
        NetworkInput* input =
            new NetworkInput();

        if (cfg)
        {
            input->SetConfig(*cfg);
        }

        return input;
    }

    // 本地文件（含 file:// 前缀）
    return new FileInput();
}

// ============================================================
// 打开 + 绑定中断回调 + 读取流信息
// ============================================================

bool InputSource::OpenWithOptions(
    const std::string& url,
    AVDictionary* opts)
{
    // 复位中断标志（支持后续 Reconnect / 重复 Open）
    abort.store(false);

    // ---------- 预分配上下文并绑定中断回调 ----------
    // 必须在 avformat_open_input 之前绑定，
    // 否则网络连接阻塞时无法打断

    fmt =
        avformat_alloc_context();

    if (!fmt)
    {
        ErrorHandler::Log(
            ErrorTag::FFmpeg,
            "avformat_alloc_context failed");

        return false;
    }

    fmt->interrupt_callback.callback =
        &InputSource::InterruptCallback;

    fmt->interrupt_callback.opaque =
        this;

    // ---------- 打开输入 ----------

    this->url =
        url;

    AVDictionary** optsPtr =
        opts ? &opts : nullptr;

    int ret =
        avformat_open_input(
            &fmt,
            url.c_str(),
            nullptr,
            optsPtr);

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Decoder,
            "avformat_open_input (" +
            protocol + ")",
            ret);

        return false;
    }

    // ---------- 读取流信息（时长 / 码率 / 流列表） ----------

    ret =
        avformat_find_stream_info(
            fmt,
            nullptr);

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Decoder,
            "avformat_find_stream_info (" +
            protocol + ")",
            ret);

        return false;
    }

    // 实时流判定：
    //   RTSP / RTMP 协议本身是直播；HTTP(S) 下播放 HLS 直播时
    //   find_stream_info 后 duration 仍 <= 0，同样按直播处理
    live =
        protocol == "rtsp" ||
        protocol == "rtmp" ||
        fmt->duration <= 0;

    // 可 Seek 判定：只有已知时长的点播流才能 Seek
    seekable =
        fmt->duration > 0;

    return true;
}

// ============================================================
// 中断回调
//
// FFmpeg 在网络 I/O 阻塞期间会周期性调用本回调，
// 返回 1 表示"用户请求中断"，av_read_frame 立即返回错误。
// ============================================================

int InputSource::InterruptCallback(
    void* opaque)
{
    InputSource* self =
        static_cast<InputSource*>(opaque);

    return self->abort.load() ? 1 : 0;
}

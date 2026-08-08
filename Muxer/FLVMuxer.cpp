#include "Muxer/FLVMuxer.h"

#include "Utils/ErrorHandler.h"
#include "Utils/Logger.h"

// ============================================================
// FLVMuxer - FLV 封装器
// ============================================================

FLVMuxer::FLVMuxer()
{
}

bool FLVMuxer::OpenOutput(
    const std::string& url)
{
    Close();

    this->url = url;

    // 二级指针 API：局部裸指针中转，成功后交给 RAII 管理
    AVFormatContext* raw = nullptr;

    int ret =
        avformat_alloc_output_context2(
            &raw,
            nullptr,             // 显式指定 flv（GetFormatName）
            GetFormatName(),
            url.c_str());

    if (ret < 0 || !raw)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Muxer,
            "avformat_alloc_output_context2 (flv)",
            ret);

        return false;
    }

    fmt.reset(raw);

    Logger::Info()
        << "[FLVMuxer] Open : "
        << url
        << std::endl;

    // 打开输出 IO（文件或 rtmp:// 由协议层处理）
    ret =
        avio_open(
            &fmt->pb,
            url.c_str(),
            AVIO_FLAG_WRITE);

    if (ret < 0)
    {
        ErrorHandler::LogFFmpeg(
            ErrorTag::Muxer,
            "avio_open (flv)",
            ret);

        // RAII：失败时自动释放
        fmt.reset();

        return false;
    }

    return true;
}

bool FLVMuxer::WritePacket(
    AVPacket* pkt)
{
    if (!fmt || !pkt)
    {
        return false;
    }

    // ---------- 关键帧起始缓冲 ----------

    if (beginWithKeyFrame &&
        !headerWritten)
    {
        // 缓冲到一定量仍未等到关键帧：放弃等待，直接开始
        // （防死等：极端情况源流一直无关键帧）
        if (pendingBytes >= kMaxPendingBytes)
        {
            Logger::Warn()
                << "[FLVMuxer] Pending buffer full, "
                << "start without key frame"
                << std::endl;

            beginWithKeyFrame = false;
        }
        else
        {
            bool isVideoKeyFrame =
                pkt->stream_index == 0 &&
                (pkt->flags & AV_PKT_FLAG_KEY);

            // 关键帧到达：开始写
            if (isVideoKeyFrame)
            {
                // 写文件头（内部会置 headerWritten）
                if (!WriteHeader())
                {
                    return false;
                }

                Logger::Info()
                    << "[FLVMuxer] Start with key frame"
                    << std::endl;

                // 按序冲刷缓冲的包
                for (AVPacket* p : pending)
                {
                    if (!Muxer::WritePacket(p))
                    {
                        break;
                    }
                }

                ClearPending();
            }
            else
            {
                // 等待关键帧：缓冲当前包（拷贝，保持所有权清晰）
                AVPacket* copy =
                    av_packet_clone(pkt);

                if (copy)
                {
                    pending.push_back(copy);

                    pendingBytes += copy->size;
                }

                return true;
            }
        }
    }

    return Muxer::WritePacket(pkt);
}

void FLVMuxer::Close()
{
    ClearPending();

    Muxer::Close();

    beginWithKeyFrame = false;
}

void FLVMuxer::BeginWithKeyFrame(
    bool enabled)
{
    beginWithKeyFrame = enabled;
}

const char* FLVMuxer::GetFormatName() const
{
    return "flv";
}

// ---------- 内部：清空起始缓冲 ----------

void FLVMuxer::ClearPending()
{
    for (AVPacket* p : pending)
    {
        av_packet_free(&p);
    }

    pending.clear();

    pendingBytes = 0;
}

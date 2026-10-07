#include "recording/OutputPipeline.h"

#include "recording/VideoEncoder.h"
#include "recording/AudioEncoder.h"
#include "recording/FLVMuxer.h"
#include "recording/HLSMuxer.h"
#include "recording/RTMPPublisher.h"
#include "config/ConfigManager.h"
#include "config/StreamConfig.h"
#include "infra/ErrorHandler.h"
#include "infra/Logger.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <vector>

OutputPipeline::OutputPipeline(
    ConfigManager* config)
    : config(config)
{
}

OutputPipeline::~OutputPipeline() = default;

void OutputPipeline::SetSourceInfo(
    const SourceInfo& info)
{
    srcInfo = info;
}
// 从编码器上下文拷贝一�?codecpar（调用方 avcodec_parameters_free 释放�?
static AVCodecParameters* CopyCodecPar(
    AVCodecContext* ctx)
{
    if (!ctx)
    {
        return nullptr;
    }

    AVCodecParameters* par =
        avcodec_parameters_alloc();

    if (!par)
    {
        return nullptr;
    }

    if (avcodec_parameters_from_context(
        par, ctx) < 0)
    {
        avcodec_parameters_free(&par);

        return nullptr;
    }

    return par;
}

bool OutputPipeline::EnsureOutEncoders()
{
    if (!srcInfo.hasVideo)
    {
        return false;
    }

    if (outVideoEncoder && outAudioEncoder)
    {
        return true;
    }

    StreamConfig cfg =
        config ?
        config->GetStreamConfig() :
        StreamConfig();

    // ---------- video encoder ----------

    if (!outVideoEncoder)
    {
        int fps = srcInfo.fps > 0 ? srcInfo.fps : 25;

        outVideoEncoder =
            std::make_unique<VideoEncoder>();

        if (!outVideoEncoder->Init(
            srcInfo.width,
            srcInfo.height,
            { 1, fps },
            cfg.videoCodec,
            cfg.bitrateKbps,
            true))
        {
            ErrorHandler::Log(
                ErrorTag::Encoder,
                "VideoEncoder init failed : " +
                cfg.videoCodec);

            outVideoEncoder.reset();

            return false;
        }

        Logger::Info()
            << "[Player] Output video encoder : "
            << outVideoEncoder->GetCodecName()
            << " (" << srcInfo.width << "x" << srcInfo.height
            << " @ " << fps << "fps, "
            << cfg.bitrateKbps << "kbps)"
            << std::endl;
    }

    // ---------- audio encoder (video-only if no audio) ----------

    if (!outAudioEncoder && srcInfo.hasAudio)
    {
        int sr =
            srcInfo.sampleRate > 0 ?
            srcInfo.sampleRate :
            48000;

        int ch =
            srcInfo.channels > 0 ?
            srcInfo.channels :
            2;

        outAudioEncoder =
            std::make_unique<AudioEncoder>();

        if (!outAudioEncoder->Init(
            sr,
            ch,
            cfg.audioCodec,
            0))
        {
            Logger::Warn()
                << "[Player] AudioEncoder init failed, "
                << "video-only output"
                << std::endl;

            outAudioEncoder.reset();
        }
        else
        {
            Logger::Info()
                << "[Player] Output audio encoder : "
                << outAudioEncoder->GetCodecName()
                << " (" << sr << "Hz / " << ch << "ch)"
                << std::endl;
        }
    }

    return outVideoEncoder != nullptr;
}
AVFrame* OutputPipeline::ToYuv420p(
    AVFrame* frame)
{
    if (!frame)
    {
        return nullptr;
    }

    // 已是 YUV420P：直通，零拷�?
    if (frame->format == AV_PIX_FMT_YUV420P)
    {
        return frame;
    }

    // 惰性创建转换器
    if (!outSws)
    {
        outSws.reset(
            sws_getContext(
                frame->width,
                frame->height,
                static_cast<AVPixelFormat>(
                    frame->format),
                frame->width,
                frame->height,
                AV_PIX_FMT_YUV420P,
                SWS_BILINEAR,
                nullptr,
                nullptr,
                nullptr));

        if (!outSws)
        {
            return nullptr;
        }

        outYuvFrame.reset(
            av_frame_alloc());

        if (!outYuvFrame)
        {
            return nullptr;
        }

        outYuvFrame->format =
            AV_PIX_FMT_YUV420P;

        outYuvFrame->width =
            frame->width;

        outYuvFrame->height =
            frame->height;

        if (av_frame_get_buffer(
            outYuvFrame.get(), 32) < 0)
        {
            return nullptr;
        }
    }

    sws_scale(
        outSws.get(),
        frame->data,
        frame->linesize,
        0,
        frame->height,
        outYuvFrame->data,
        outYuvFrame->linesize);

    return outYuvFrame.get();
}

void OutputPipeline::FeedOutputVideo(
    AVFrame* frame)
{
    if (!frame)
    {
        return;
    }

    // 视频/音频解码线程并发�?muxer，必须加�?
    // （StopAllOutputs 在解码线程停止后调用，锁内调 Flush
    // 不会死锁�?
    std::lock_guard<std::mutex> lock(
        outMutex);

    if (!recording &&
        !pushing &&
        !hlsActive)
    {
        return;
    }

    if (!outVideoEncoder)
    {
        return;
    }

    AVFrame* yuv =
        ToYuv420p(frame);

    if (!yuv)
    {
        return;
    }

    // 自管�?pts：按帧号递增（time_base = 1/fps�?
    yuv->pts =
        outVideoPts++;

    if (!outVideoEncoder->Encode(yuv))
    {
        return;
    }

    // 取出所有编码输出包并分�?
    AVPacket* pkt = nullptr;

    while ((pkt =
        outVideoEncoder->GetPacket()) != nullptr)
    {
        // nvenc 等编码器输出包可能不�?pts/dts
        // （AV_NOPTS_VALUE）——统一按输出顺序重建，
        // 保证 muxer 层时间戳单调（FLV/HLS 依赖�?
        pkt->pts =
            outVideoPktIdx;

        pkt->dts =
            outVideoPktIdx;

        pkt->duration = 1;

        pkt->time_base =
            outVideoEncoder->GetContext()->time_base;

        // 所有输出都是视频流在前（index 0�?
        pkt->stream_index = 0;

        outVideoPktIdx++;

        DispatchVideoPacket(pkt);

        av_packet_free(&pkt);
    }
}

void OutputPipeline::FeedOutputAudio(
    AVFrame* frame)
{
    if (!frame)
    {
        return;
    }

    // �?FeedOutputVideo 同一把锁（见其注释）
    std::lock_guard<std::mutex> lock(
        outMutex);

    if (!recording &&
        !pushing &&
        !hlsActive)
    {
        return;
    }

    if (!outAudioEncoder)
    {
        return;
    }

    // 注意�?*不要**修改 frame->pts！这是播放路径共享的解码帧，
    // 改动会让 SyncController 音视频失步（实测 -25s 漂移）�?
    // aac 编码器输出包 pts 继承输入�?pts（解码帧 pts 正常），
    // 无需自管理�?
    if (!outAudioEncoder->Encode(frame))
    {
        return;
    }

    AVPacket* pkt = nullptr;

    while ((pkt =
        outAudioEncoder->GetPacket()) != nullptr)
    {
        // 同样兜底：音频包 pts 缺失时按输出顺序重建
        if (pkt->pts == AV_NOPTS_VALUE)
        {
            pkt->pts =
                outAudioPktIdx;

            pkt->dts =
                outAudioPktIdx;
        }

        // 音频包固定写�?index 1（编码器默认 0，必须改�?
        pkt->stream_index = 1;

        outAudioPktIdx++;

        DispatchAudioPacket(pkt);

        av_packet_free(&pkt);
    }
}

void OutputPipeline::DispatchVideoPacket(
    AVPacket* pkt)
{
    if (!pkt)
    {
        return;
    }

    // 注意：av_interleaved_write_frame 会消费并清空 pkt 字段�?
    // 同一包发给多�?muxer 必须逐输�?clone，否则后写的
    // muxer 拿到 pts=NOPTS/duration=0 的脏�?
    if (recording && recordMuxer)
    {
        AVPacket* copy =
            av_packet_clone(pkt);

        if (copy)
        {
            recordMuxer->WritePacket(copy);

            av_packet_free(&copy);
        }
    }

    if (pushing && rtmpPublisher)
    {
        AVPacket* copy =
            av_packet_clone(pkt);

        if (copy)
        {
            rtmpPublisher->PushPacket(copy);

            av_packet_free(&copy);
        }
    }

    if (hlsActive && hlsMuxer)
    {
        AVPacket* copy =
            av_packet_clone(pkt);

        if (copy)
        {
            hlsMuxer->WritePacket(copy);

            av_packet_free(&copy);
        }
    }
}

void OutputPipeline::DispatchAudioPacket(
    AVPacket* pkt)
{
    DispatchVideoPacket(pkt);
}

void OutputPipeline::FlushOutEncoders()
{
    // 视频编码器尾�?
    if (outVideoEncoder)
    {
        outVideoEncoder->Flush();

        AVPacket* pkt = nullptr;

        while ((pkt =
            outVideoEncoder->GetPacket()) != nullptr)
        {
            // flush 包同样重建时间戳（编码器输出可能�?pts�?
            pkt->pts =
                outVideoPktIdx;

            pkt->dts =
                outVideoPktIdx;

            pkt->duration = 1;

            pkt->time_base =
                outVideoEncoder->GetContext()->time_base;

            pkt->stream_index = 0;

            outVideoPktIdx++;

            DispatchVideoPacket(pkt);

            av_packet_free(&pkt);
        }
    }

    // 音频编码器尾�?
    if (outAudioEncoder)
    {
        outAudioEncoder->Flush();

        AVPacket* pkt = nullptr;

        while ((pkt =
            outAudioEncoder->GetPacket()) != nullptr)
        {
            if (pkt->pts == AV_NOPTS_VALUE)
            {
                pkt->pts =
                    outAudioPktIdx;

                pkt->dts =
                    outAudioPktIdx;
            }

            pkt->stream_index = 1;

            outAudioPktIdx++;

            DispatchAudioPacket(pkt);

            av_packet_free(&pkt);
        }
    }
}

void OutputPipeline::StopAllOutputs()
{
    std::lock_guard<std::mutex> lock(
        outMutex);

    if (!recording &&
        !pushing &&
        !hlsActive)
    {
        return;
    }

    // 冲刷尾帧（写给仍在活跃的输出�?
    FlushOutEncoders();

    if (recordMuxer)
    {
        recordMuxer->WriteTrailer();

        recordMuxer->Close();

        recordMuxer.reset();
    }

    if (rtmpPublisher)
    {
        rtmpPublisher->Stop();

        rtmpPublisher.reset();
    }

    if (hlsMuxer)
    {
        hlsMuxer->WriteTrailer();

        hlsMuxer->Close();

        hlsMuxer.reset();
    }

    recording = false;

    pushing = false;

    hlsActive = false;

    ReleaseOutEncoders();

    Logger::Info()
        << "[Player] All outputs stopped"
        << std::endl;
}

void OutputPipeline::ReleaseOutEncoders()
{
    if (outVideoEncoder)
    {
        outVideoEncoder->Close();
    }

    outVideoEncoder.reset();

    if (outAudioEncoder)
    {
        outAudioEncoder->Close();
    }

    outAudioEncoder.reset();

    outSws.reset();

    outYuvFrame.reset();

    outVideoPts = 0;

    outAudioPts = 0;

    outVideoPktIdx = 0;

    outAudioPktIdx = 0;
}

bool OutputPipeline::StartRecording(
    const std::string& path)
{
    std::lock_guard<std::mutex> lock(
        outMutex);

    if (recording)
    {
        return false;
    }

    if (!EnsureOutEncoders())
    {
        return false;
    }

    recordMuxer =
        std::make_unique<FLVMuxer>();

    if (!recordMuxer->OpenOutput(path))
    {
        recordMuxer.reset();

        return false;
    }

    AVCodecContext* vc =
        outVideoEncoder->GetContext();

    // 编码器上下文 -> codecpar（AddStream 内部拷贝�?
    AVCodecParameters* vPar =
        CopyCodecPar(vc);

    recordMuxer->AddVideoStream(
        vPar,
        vc->time_base);

    avcodec_parameters_free(&vPar);

    if (outAudioEncoder)
    {
        AVCodecParameters* aPar =
            CopyCodecPar(
                outAudioEncoder->GetContext());

        if (aPar)
        {
            recordMuxer->AddAudioStream(aPar);

            avcodec_parameters_free(&aPar);
        }
    }

    if (!recordMuxer->WriteHeader())
    {
        recordMuxer->Close();

        recordMuxer.reset();

        return false;
    }

    recording = true;

    Logger::Info()
        << "[Player] Recording start : "
        << path
        << std::endl;

    return true;
}

void OutputPipeline::StopRecording()
{
    std::lock_guard<std::mutex> lock(
        outMutex);

    if (!recording)
    {
        return;
    }

    // 先冲刷尾帧（此时 recording 仍为 true�?
    // 尾帧�?Dispatch 写向本路 + 其余活跃输出�?
    FlushOutEncoders();

    recording = false;

    if (recordMuxer)
    {
        recordMuxer->WriteTrailer();

        recordMuxer->Close();

        recordMuxer.reset();
    }

    // 没有其他输出在用时释放编码器
    if (!pushing && !hlsActive)
    {
        ReleaseOutEncoders();
    }

    Logger::Info()
        << "[Player] Recording stopped"
        << std::endl;
}

void OutputPipeline::ToggleRecording()
{
    if (IsRecording())
    {
        StopRecording();

        return;
    }

    // 生成时间戳文件名 record_YYYYMMDD_HHMMSS.flv
    char buf[64] = { 0 };

    std::time_t t =
        std::time(nullptr);

    std::tm local = { 0 };

    localtime_s(&local, &t);

    std::snprintf(
        buf,
        sizeof(buf),
        "record_%04d%02d%02d_%02d%02d%02d.flv",
        local.tm_year + 1900,
        local.tm_mon + 1,
        local.tm_mday,
        local.tm_hour,
        local.tm_min,
        local.tm_sec);

    StartRecording(buf);
}

bool OutputPipeline::IsRecording() const
{
    return recording;
}

bool OutputPipeline::StartPushing(
    const std::string& url)
{
    std::lock_guard<std::mutex> lock(
        outMutex);

    if (pushing)
    {
        return false;
    }

    if (!EnsureOutEncoders())
    {
        return false;
    }

    StreamConfig cfg =
        config ?
        config->GetStreamConfig() :
        StreamConfig();

    std::string target =
        url.empty() ?
        cfg.rtmpUrl :
        url;

    if (target.empty())
    {
        Logger::Warn()
            << "[Player] No rtmp_url in stream.json"
            << std::endl;

        return false;
    }

    rtmpPublisher =
        std::make_unique<RTMPPublisher>();

    rtmpPublisher->SetConfig(cfg);

    if (!rtmpPublisher->Connect(target))
    {
        ErrorHandler::Log(
            ErrorTag::Network,
            "RTMP connect failed : " +
            target);

        rtmpPublisher.reset();

        return false;
    }

    AVCodecContext* vc =
        outVideoEncoder->GetContext();

    AVCodecParameters* vPar =
        CopyCodecPar(vc);

    rtmpPublisher->AddVideoStream(
        vPar,
        vc->time_base);

    avcodec_parameters_free(&vPar);

    if (outAudioEncoder)
    {
        AVCodecParameters* aPar =
            CopyCodecPar(
                outAudioEncoder->GetContext());

        if (aPar)
        {
            rtmpPublisher->AddAudioStream(aPar);

            avcodec_parameters_free(&aPar);
        }
    }

    if (!rtmpPublisher->Start())
    {
        rtmpPublisher->Stop();

        rtmpPublisher.reset();

        return false;
    }

    pushing = true;

    Logger::Info()
        << "[Player] Pushing start : "
        << target
        << std::endl;

    return true;
}

void OutputPipeline::StopPushing()
{
    std::lock_guard<std::mutex> lock(
        outMutex);

    if (!pushing)
    {
        return;
    }

    // 先冲刷尾帧（pushing 仍为 true，尾帧写向本路）
    FlushOutEncoders();

    pushing = false;

    if (rtmpPublisher)
    {
        rtmpPublisher->Stop();
    }

    rtmpPublisher.reset();

    if (!recording && !hlsActive)
    {
        ReleaseOutEncoders();
    }

    Logger::Info()
        << "[Player] Pushing stopped"
        << std::endl;
}

void OutputPipeline::TogglePushing()
{
    if (IsPushing())
    {
        StopPushing();

        return;
    }

    StartPushing("");
}

bool OutputPipeline::IsPushing() const
{
    return pushing;
}

bool OutputPipeline::StartHLS(
    const std::string& dir)
{
    std::lock_guard<std::mutex> lock(
        outMutex);

    if (hlsActive)
    {
        return false;
    }

    if (!EnsureOutEncoders())
    {
        return false;
    }

    StreamConfig cfg =
        config ?
        config->GetStreamConfig() :
        StreamConfig();

    // 创建输出目录
    std::error_code ec;

    std::filesystem::create_directories(
        dir, ec);

    std::string path = dir;

    if (!path.empty() &&
        path.back() != '/' &&
        path.back() != '\\')
    {
        path += "/";
    }

    path += "index.m3u8";

    hlsMuxer =
        std::make_unique<HLSMuxer>();

    hlsMuxer->SetSegmentDuration(
        cfg.hlsSegmentDurationSec > 0 ?
        cfg.hlsSegmentDurationSec :
        4.0);

    hlsMuxer->SetListSize(
        cfg.hlsListSize);

    // 分段文件�?m3u8 同目录（hls_segment_filename 是相�?cwd 的路径，
    // 必须显式拼目录，否则 segment 会落到工作目录）
    std::string segPattern = dir;

    if (!segPattern.empty() &&
        segPattern.back() != '/' &&
        segPattern.back() != '\\')
    {
        segPattern += "/";
    }

    segPattern +=
        "segment%03d.ts";

    hlsMuxer->SetSegmentFilenamePattern(
        segPattern);

    if (!hlsMuxer->OpenOutput(path))
    {
        hlsMuxer.reset();

        return false;
    }

    AVCodecContext* vc =
        outVideoEncoder->GetContext();

    AVCodecParameters* vPar =
        CopyCodecPar(vc);

    hlsMuxer->AddVideoStream(
        vPar,
        vc->time_base);

    avcodec_parameters_free(&vPar);

    if (outAudioEncoder)
    {
        AVCodecParameters* aPar =
            CopyCodecPar(
                outAudioEncoder->GetContext());

        if (aPar)
        {
            hlsMuxer->AddAudioStream(aPar);

            avcodec_parameters_free(&aPar);
        }
    }

    if (!hlsMuxer->WriteHeader())
    {
        hlsMuxer->Close();

        hlsMuxer.reset();

        return false;
    }

    hlsActive = true;

    Logger::Info()
        << "[Player] HLS start : "
        << path
        << std::endl;

    return true;
}

void OutputPipeline::StopHLS()
{
    std::lock_guard<std::mutex> lock(
        outMutex);

    if (!hlsActive)
    {
        return;
    }

    // 先冲刷尾帧（hlsActive 仍为 true，尾帧写向本路）
    FlushOutEncoders();

    hlsActive = false;

    if (hlsMuxer)
    {
        hlsMuxer->WriteTrailer();

        hlsMuxer->Close();
    }

    hlsMuxer.reset();

    if (!recording && !pushing)
    {
        ReleaseOutEncoders();
    }

    Logger::Info()
        << "[Player] HLS stopped"
        << std::endl;
}

void OutputPipeline::ToggleHLS()
{
    if (IsHLSActive())
    {
        StopHLS();

        return;
    }

    StartHLS("hls_out");
}

bool OutputPipeline::IsHLSActive() const
{
    return hlsActive;
}


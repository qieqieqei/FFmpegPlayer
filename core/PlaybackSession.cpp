#include "core/PlaybackSession.h"

#include "core/Player.h"
#include "output/video/Renderer.h"
#include "infra/ErrorHandler.h"
#include "infra/Logger.h"
#include "streaming/StreamMonitor.h"
#include "hardware/CUDAContext.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

PlaybackSession::PlaybackSession(
    Player& owner,
    MediaContext& media)
    : owner(owner)
    , media(media)
{
    quit.store(false);
}

PlaybackSession::~PlaybackSession()
{
    StopThreads();
}

// ============================================================
// 打开媒体（首次初始化 / 播放列表切换共用�?
// ============================================================

bool PlaybackSession::OpenMedia(
    const std::string& path)
{
    // v2: buffer state machine reset (reconnect/switch re-enters here)
    if (owner.bufferController)
    {
        owner.bufferController->Reset();
    }

    // 记录当前媒体路径（断网重连目标）
    currentMediaPath = path;

    // 复位队列打断状�?
    media.videoPacketQueue.ResetInterrupt();

    media.audioPacketQueue.ResetInterrupt();

    media.videoFrameQueue.ResetInterrupt();

    // ---------- 解复用器 ----------

    media.demuxer =
        std::make_unique<Demuxer>();

    // 网络参数（rtsp_transport / 超时 / 低延迟，来自 stream.json�?
    if (owner.configManager)
    {
        media.demuxer->SetNetworkConfig(
            owner.configManager->GetStreamConfig());
    }

    if (!media.demuxer->Open(path))
    {
        ErrorHandler::Log(
            ErrorTag::Player,
            "Demuxer open failed : " +
            path);

        return false;
    }

    // 网络流：按直�?点播设置缓冲策略�?.3�?
    if (owner.bufferController)
    {
        owner.bufferController->SetLive(
            media.demuxer->IsLive());
    }

    // 直播流：Demux<->Decode 队列切换 NetworkBuffer（满丢最旧，低延迟）
    // 点播/本地文件：保�?PacketQueue 满阻塞背�?
    media.useNetBuffer =
        media.demuxer->IsLive();

    // 8.5：同步策略切直播 / 点播（LiveClock vs DropController�?
    if (owner.syncController)
    {
        owner.syncController->SetLiveMode(
            media.useNetBuffer);
    }

    if (media.useNetBuffer)
    {
        int cap = 600;

        int targetMs = 300;

        // v2: buffer mode - stable (default) or low_latency
        bool stableBuffer = true;

        int liveQueueMs = 3000;

        int liveMaxQueueMs = 3000;   // v2: stable mode backlog cap (default 3000ms)：直播追最新阈值（默认 500ms�?

        if (owner.configManager)
        {
            const StreamConfig& sc =
                owner.configManager->GetStreamConfig();

            cap = sc.maxBufferPackets;

            liveMaxQueueMs = sc.liveMaxQueueMs;

            targetMs = sc.bufferTargetMs;

            // v2: stable uses configured liveMaxQueueMs; low_latency keeps old 500ms
            stableBuffer =
                (sc.liveBufferMode == "low_latency") ?
                false :
                true;

            liveQueueMs =
                stableBuffer ?
                sc.liveMaxQueueMs :
                500;

            // v2: CLI override --live-buffer <ms> (0 = low_latency)
            if (owner.liveBufferOverrideMs >= 0)
            {
                stableBuffer = (owner.liveBufferOverrideMs > 0);

                if (owner.liveBufferOverrideMs > 0)
                {
                    targetMs = owner.liveBufferOverrideMs;
                }

                liveQueueMs =
                    stableBuffer ?
                    sc.liveMaxQueueMs :
                    500;
            }
        }

        // v2: buffer mode + sync strategy (stable: ahead-drop off, behind 2500ms)
        if (owner.bufferController)
        {
            owner.bufferController->SetMode(stableBuffer);
        }

        if (owner.syncController)
        {
            owner.syncController->SetBufferStableMode(stableBuffer);
        }

        media.videoNetBuffer.SetMaxSize(cap);

        // v2: memory cap (one of the three limits; default 64MB)
        media.videoNetBuffer.SetMaxMemoryBytes(64 * 1024 * 1024);

        // 8.5：视频队列时长上限——积压超�?liveQueueMs 丢旧包追最�?
        // （与包数上限叠加；GOP 感知，不撕裂解码链）
        AVStream* liveVStream =
            media.demuxer->GetVideoStream();

        if (liveVStream)
        {
            // v2: NetworkBuffer cap is mode-independent (always 3s per spec).
            // low latency comes from low water marks, NOT from shrinking the
            // network buffer (shrinking to 500ms caused constant rebuffering
            // oscillation around the high-water mark).
            media.videoNetBuffer.SetLiveDurationMs(
                liveMaxQueueMs,
                liveVStream->time_base.num,
                liveVStream->time_base.den);
        }

        // 8.5：音频队列切 LiveMode（PacketQueue 直播模式）：
        // Push 不阻塞，积压超过 liveQueueMs 丢旧包；
        // 音频帧无解码依赖，丢弃安�?
        AVStream* liveAStream =
            media.demuxer->GetAudioStream();

        if (liveAStream)
        {
            media.audioPacketQueue.SetLiveMode(
                true,
                liveAStream->time_base.num,
                liveAStream->time_base.den,
                liveQueueMs);
        }
        else
        {
            media.audioPacketQueue.SetLiveMode(
                true,
                1,
                90000,
                liveQueueMs);
        }

        // 直播目标缓冲（覆盖默�?300ms，按配置�?
        if (owner.bufferController && stableBuffer)
        {
            owner.bufferController->SetTargetBufferMs(
                targetMs);
        }

        Logger::Info()
            << "[Player] Live buffer : NetworkBuffer "
            << "cap=" << cap
            << " target=" << targetMs
            << "ms"
            << " liveQueue=" << liveQueueMs
            << "ms (drop old to chase latest)"
            << " mode=" << (stableBuffer ? "stable" : "low_latency")
            << std::endl;

        // v2: startup pre-buffer - gate holds audio pause + decode until highWater
        if (owner.bufferController)
        {
            owner.bufferController->Start();
        }
    }

    if (owner.networkStatistics)
    {
        owner.networkStatistics->Reset();
    }

    // 流媒体监控：切换媒体时复位告�?/ 活性计�?
    if (owner.streamMonitor)
    {
        owner.streamMonitor->Reset();
    }

    if (media.demuxer->IsNetwork())
    {
        Logger::Info()
            << "[Player] Network stream : "
            << media.demuxer->GetProtocol()
            << (media.demuxer->IsLive() ?
                " (live)" :
                " (vod)")
            << std::endl;
    }

    media.duration =
        media.demuxer->GetDuration();

    Logger::Info()
        << "[Player] Duration : "
        << media.duration
        << " s"
        << std::endl;

    AVStream* vStream =
        media.demuxer->GetVideoStream();

    // ---------- 视频解码�?----------

    // 硬件解码优先（配置开�?+ CUDA 可用 + h264/hevc），
    // 失败自动回退下面的软�?
    TryInitHardwareDecoder(
        vStream->codecpar);

    media.videoDecoder =
        std::make_unique<VideoDecoder>();

    // 8.5：直播解码级低延迟（avcodec_open2 �?flags=low_delay�?
    media.videoDecoder->SetLowDelay(
        media.useNetBuffer);

    if (!media.videoDecoder->Init(
        vStream->codecpar))
    {
        ErrorHandler::Log(
            ErrorTag::Player,
            "VideoDecoder init failed");

        return false;
    }

    AVCodecContext* vCtx =
        media.videoDecoder->GetContext();

    // 直播重连可能拿到未解�?SPS 的流�?x0）：提前失败�?
    // 走重连循环重试（等下一个关键帧�?
    if (vCtx->width <= 0 ||
        vCtx->height <= 0)
    {
        Logger::Warn()
            << "[Player] Video stream not ready "
            << "(0x0), will retry"
            << std::endl;

        return false;
    }

    // ---------- 播放统计�?.7�?----------

    owner.statistics =
        std::make_unique<PlayerStatistics>();

    owner.statistics->Init(
        media.demuxer->GetFormatContext(),
        media.demuxer->GetVideoIndex(),
        media.demuxer->GetAudioIndex());

    // 无音频时按帧率匀速播�?
    AVRational fpsRat =
        av_guess_frame_rate(
            media.demuxer->GetFormatContext(),
            vStream,
            nullptr);

    double fps =
        (fpsRat.num > 0 && fpsRat.den > 0) ?
        av_q2d(fpsRat) :
        25.0;

    media.videoFrameDuration =
        1.0 / fps;

    Logger::Info()
        << "[Player] Video Frame Duration : "
        << media.videoFrameDuration
        << " s"
        << std::endl;

    // ---------- YUV -> RGB 转换�?----------
    // （惰性创建：GetSwsForFrame 按实际帧格式建，
    //   硬解�?NV12 / 软解�?YUV420P 自动适配�?

    owner.rgbLinesize =
        vCtx->width * 3;

    owner.rgbData =
        std::make_unique<uint8_t[]>(
            owner.rgbLinesize * vCtx->height);

    // ---------- SDL 窗口 / 渲染�?----------

    if (!InitSDL(
        vCtx->width,
        vCtx->height,
        owner.window,
        owner.renderer,
        owner.texture))
    {
        return false;
    }

    owner.rgbTexture =
        SDL_CreateTexture(
            owner.renderer,
            SDL_PIXELFORMAT_RGB24,
            SDL_TEXTUREACCESS_STREAMING,
            vCtx->width,
            vCtx->height);

    if (!owner.rgbTexture)
    {
        ErrorHandler::LogSDL(
            ErrorTag::Player,
            "SDL_CreateTexture (RGB)");

        return false;
    }

    // 切换媒体后恢复全屏状�?
    if (owner.fullscreen)
    {
        SDL_SetWindowFullscreen(
            owner.window,
            SDL_WINDOW_FULLSCREEN_DESKTOP);
    }

    // ---------- 音频链路�?.0 独立 Audio 线程�?----------

    media.hasAudioStream =
        media.demuxer->HasAudio();

    if (media.hasAudioStream)
    {
        // 音频解码�?
        media.audioDecoder =
            std::make_unique<AudioDecoder>();

        if (!media.audioDecoder->Init(
            media.demuxer->GetAudioStream()->codecpar))
        {
            ErrorHandler::Log(
                ErrorTag::Audio,
                "AudioDecoder init failed, video only");

            media.audioDecoder.reset();

            media.hasAudioStream = false;
        }
    }

    if (media.hasAudioStream)
    {
        // SDL 音频设备（与重采样器输出一致：48000Hz / 双声�?/ S16�?
        media.audioDevice =
            std::make_unique<AudioDevice>();

        if (!media.audioDevice->Init(
            48000,
            2))
        {
            ErrorHandler::Log(
                ErrorTag::Audio,
                "AudioDevice init failed, video only");

            media.audioDevice.reset();

            media.hasAudioStream = false;
        }
    }

    if (media.hasAudioStream)
    {
        // 重采样器
        media.audioResampler =
            std::make_unique<AudioResampler>();

        // 变速不变调（SOLA），组合包装�?
        media.speedController =
            std::make_unique<SpeedController>();

        media.speedController->Init(
            48000,
            2);

        // 恢复用户设置的速度 / 音量
        media.speedController->SetSpeed(owner.playbackSpeed);

        media.audioDevice->SetSpeedFactor(owner.playbackSpeed);

        media.audioDevice->SetVolume(owner.volume);
    }

    if (!media.hasAudioStream)
    {
        Logger::Warn()
            << "[Player] Video only mode"
            << std::endl;
    }

    // 8.1：同步控制器绑定音频主时钟（无音频时解绑�?
    // MasterClock 自动回退视频时钟�?
    owner.syncController->SetAudioClock(
        media.audioDevice ?
        media.audioDevice->GetClock() :
        nullptr);

    // ---------- Seek 控制器绑�?----------
    // 队列是成员对象（地址不变）；demuxer 每次重建需重新绑定

    owner.seekController->Attach(
        media.demuxer.get(),
        &media.videoPacketQueue,
        &media.audioPacketQueue,
        &media.videoFrameQueue);

    // ---------- 字幕自动加载（同路径 .srt / .ass�?----------

    if (owner.subtitleManager)
    {
        owner.subtitleManager->Clear();

        std::string base =
            path;

        size_t dot =
            base.find_last_of('.');

        if (dot != std::string::npos)
        {
            base =
                base.substr(0, dot);
        }

        if (!owner.subtitleManager->Load(
            base + ".srt"))
        {
            owner.subtitleManager->Load(
                base + ".ass");
        }
    }

    // ---------- 状态复�?----------

    owner.currentTime = 0.0;

    owner.progress = 0.0;

    seekPending = false;

    seekPosition = 0.0;

    dropAudioUntil = -1.0;

    videoEof.store(false);

    audioEof.store(false);

    demuxEof.store(false);

    owner.frameStepRequest = false;

    autoAdvancing = false;

    return true;
}
// ============================================================
// 线程：启�?/ 停止
// ============================================================

bool PlaybackSession::StartThreads()
{
    quit.store(false);

    demuxThread =
        std::thread(
            &PlaybackSession::DemuxLoop,
            this);

    videoThread =
        std::thread(
            &PlaybackSession::VideoDecodeLoop,
            this);

    audioThread =
        std::thread(
            &PlaybackSession::AudioDecodeLoop,
            this);

    Logger::Info()
        << "[Player] Threads Started (Demux + Video + Audio)"
        << std::endl;

    return true;
}
void PlaybackSession::StopThreads()
{
    if (!demuxThread.joinable() &&
        !videoThread.joinable() &&
        !audioThread.joinable())
    {
        return;
    }

    quit.store(true);

    // 打断所有阻塞调用，唤醒线程退�?
    media.videoPacketQueue.Interrupt();

    media.audioPacketQueue.Interrupt();

    media.videoFrameQueue.Interrupt();

    // 直播队列（NetworkBuffer）同样打�?
    media.videoNetBuffer.Interrupt();

    // 打断网络流的阻塞读取（av_read_frame 会立即返回）
    // 否则 RTSP/HTTP 断线或超时时 join 会卡�?
    if (media.demuxer)
    {
        media.demuxer->SetAbort(true);
    }

    if (demuxThread.joinable())
    {
        demuxThread.join();
    }

    if (videoThread.joinable())
    {
        videoThread.join();
    }

    if (audioThread.joinable())
    {
        audioThread.join();
    }

    Logger::Info()
        << "[Player] Threads Stopped"
        << std::endl;
}
// ============================================================
// Demux 线程主循�?
//
//   1. 处理 Seek 请求（优先）
//   2. av_read_frame 读一个包
//   3. 视频�?-> videoPacketQueue
//   4. 音频�?-> audioPacketQueue
//   5. EOF -> 标记 demuxEof
// ============================================================

void PlaybackSession::DemuxLoop()
{
    while (!quit.load())
    {
        // ---------- Seek 处理（优先） ----------

        if (owner.seekController &&
            owner.seekController->HasRequest())
        {
            owner.seekController->Execute();

            // Seek 后继续读，不再是 EOF
            demuxEof.store(false);
        }

        if (quit.load())
        {
            break;
        }

        if (!media.demuxer)
        {
            break;
        }

        // ---------- 读一个包 ----------

        // 8.4：PacketPtr RAII，所有权随包流转（Demux -> 队列 -> 解码器）
        PacketPtr pkt(
            av_packet_alloc());

        int ret =
            media.demuxer->ReadPacket(pkt.get());

        if (ret < 0)
        {
            // pkt 作用域结束自动释�?

            if (ret == AVERROR_EOF)
            {
                // 文件读完了：标记 EOF（音频线程负责冲刷解码器�?
                demuxEof.store(true);
            }
            else
            {
                ErrorHandler::LogFFmpeg(
                    ErrorTag::Player,
                    "av_read_frame",
                    ret);
            }

            // ---------- 断网重连�?.3）：直播流报�?EOF 触发 ----------

            if (media.useNetBuffer &&
                media.demuxer &&
                media.demuxer->IsNetwork())
            {
                Logger::Warn()
                    << "[Player] Network stream error, "
                    << "requesting reconnect"
                    << std::endl;

                reconnectRequested.store(true);

                // 打断阻塞读，退�?Demux 线程（主循环负责重建�?
                media.demuxer->SetAbort(true);

                break;
            }

            // 短暂等待，让 Seek 请求有机会被处理
            SDL_Delay(2);

            continue;
        }

        // ---------- 分发�?----------

        // 网络统计：收到一个包�?.2�?
        if (owner.networkStatistics)
        {
            owner.networkStatistics->OnPacketReceived(
                pkt->size);
        }

        // v2: buffer state machine - refresh stall timer on every packet
        if (owner.bufferController)
        {
            owner.bufferController->OnPacketReceived();
        }

        if (pkt->stream_index ==
            media.demuxer->GetVideoIndex())
        {
            // 视频包入队（直播：NetworkBuffer 满丢最旧；点播：背压）
            // 8.4：失败时 pkt 仍归本作用域，RAII 自动释放
            PushVideoPacket(std::move(pkt));
        }
        else if (
            pkt->stream_index ==
            media.demuxer->GetAudioIndex())
        {
            // 音频包入队（直播：NetworkBuffer；点播：背压�?
            PushAudioPacket(std::move(pkt));
        }
        else
        {
            // 其他流（字幕等）直接丢弃（RAII 自动释放�?
        }
    }

    Logger::Info()
        << "[Player] Demux thread exit"
        << std::endl;
}
// ============================================================
// Video 线程主循�?
//
//   1. �?videoPacketQueue 取包
//   2. avcodec_send_packet + avcodec_receive_frame
//   3. 帧入 videoFrameQueue（Render 线程消费�?
//
//   Seek 时队列会�?Interrupt�?
//   - 立即 flush 视频解码器（丢弃 Seek 前的解码状态）
//   - 等待队列恢复后继�?
// ============================================================

// ============================================================
// 硬件解码接入辅助�?.7�?
//
// 三个辅助函数统一“当前激活的视频解码器”入口：
//   - hwDecoder 激活（硬解成功）时走硬解，Receive 后把 GPU �?
//     拷回系统内存（NV12），调用方拿到的是可直接渲染/编码的帧�?
//   - 否则走软�?videoDecoder（原逻辑不变）�?
// ============================================================

void PlaybackSession::FlushVideoDecoder()
{
    if (media.hwDecoder &&
        media.hwDecoder->IsReady())
    {
        media.hwDecoder->Flush();

        return;
    }

    if (media.videoDecoder)
    {
        media.videoDecoder->Flush();
    }
}
bool PlaybackSession::SendVideoPacket(
    AVPacket* pkt)
{
    if (media.hwDecoder &&
        media.hwDecoder->IsReady())
    {
        return media.hwDecoder->SendPacket(pkt);
    }

    return media.videoDecoder ?
        media.videoDecoder->SendPacket(pkt) :
        false;
}
DecodeResult PlaybackSession::ReceiveVideoFrame(
    FramePtr& out)
{
    if (media.hwDecoder &&
        media.hwDecoder->IsReady())
    {
        // 硬件路径：先�?GPU 帧，再回读到系统内存（NV12�?
        DecodeResult r =
            media.hwDecoder->ReceiveFrame(out);

        if (r != DecodeResult::Success)
        {
            return r;
        }

        // 惰性创建回读目标帧
        if (!media.hwTransferFrame)
        {
            media.hwTransferFrame.reset(
                av_frame_alloc());

            if (!media.hwTransferFrame)
            {
                return DecodeResult::Error;
            }
        }

        // GPU �?-> 系统内存（软解模式直�?ref�?
        if (!media.hwDecoder->TransferFrame(
            out.get(),
            media.hwTransferFrame.get()))
        {
            return DecodeResult::Error;
        }

        // 回读帧交给调用方（GPU �?out 自动释放�?
        out.reset(
            av_frame_clone(
                media.hwTransferFrame.get()));

        av_frame_unref(
            media.hwTransferFrame.get());

        if (!out)
        {
            return DecodeResult::Error;
        }

        return DecodeResult::Success;
    }

    return media.videoDecoder ?
        media.videoDecoder->ReceiveFrame(out) :
        DecodeResult::Error;
}
void PlaybackSession::TryInitHardwareDecoder(
    AVCodecParameters* codecpar)
{
    if (!codecpar ||
        media.hwDecoder)
    {
        return;
    }

    // 配置开�?
    StreamConfig cfg =
        owner.configManager ?
        owner.configManager->GetStreamConfig() :
        StreamConfig();

    if (!cfg.hardwareDecode)
    {
        Logger::Info()
            << "[Player] Hardware decode disabled "
            << "by config"
            << std::endl;

        return;
    }

    // 硬件探测（CUDA -> D3D11VA -> DXVA2�?
    if (!owner.cudaContext ||
        !owner.cudaContext->IsAvailable())
    {
        Logger::Info()
            << "[Player] No hardware device, "
            << "use software decode"
            << std::endl;

        return;
    }

    // �?h264 / hevc 走硬�?
    std::string codecName;

    switch (codecpar->codec_id)
    {
    case AV_CODEC_ID_H264:
        codecName = "h264";
        break;

    case AV_CODEC_ID_HEVC:
        codecName = "hevc";
        break;

    default:
        return;
    }

    if (codecpar->width <= 0 ||
        codecpar->height <= 0)
    {
        return;
    }

    media.hwDecoder =
        std::make_unique<HardwareDecoder>();

    // 8.5：直播解码级低延迟（硬件 + 软解回退两处 avcodec_open2�?
    media.hwDecoder->SetLowDelay(
        media.useNetBuffer);

    if (!media.hwDecoder->Init(
        owner.cudaContext.get(),
        codecName,
        codecpar))
    {
        Logger::Warn()
            << "[Player] Hardware decoder init "
            << "failed, use software"
            << std::endl;

        media.hwDecoder.reset();
    }
}
void PlaybackSession::VideoDecodeLoop()
{
    // Seek 代数：每�?Seek 递增，用于判断是否需�?flush
    int lastSeekGen = 0;

    // �?EOF 周期是否已冲刷过解码�?
    bool eofFlushed = false;

    while (!quit.load())
    {
        // ---------- 检查是否有新的 Seek ----------

        int seekGen =
            owner.seekController ?
            owner.seekController->GetGeneration() :
            0;

        if (seekGen != lastSeekGen)
        {
            // 新的一�?Seek：清空解码器内部状�?
            FlushVideoDecoder();

            lastSeekGen = seekGen;

            eofFlushed = false;

            // 解码未结�?
            videoEof.store(false);
        }

        // ---------- 取视频包 ----------

        // 8.4：PacketPtr 返回所有权，无需手动释放
        // v2: buffer gate - hold video consumption during prebuffer/rebuffer/stall
        while (media.useNetBuffer &&
            owner.bufferController &&
            owner.bufferController->IsConsumingBlocked())
        {
            static Uint32 lastVGateLog = 0;

            Uint32 vNow = SDL_GetTicks();

            if (vNow - lastVGateLog >= 500)
            {
                lastVGateLog = vNow;

                Logger::Info()
                    << "[Gate] VDecode held state="
                    << owner.bufferController->GetStateName()
                    << std::endl;
            }

            SDL_Delay(2);

            if (quit.load())
            {
                break;
            }
        }

        PacketPtr pkt =
            PopVideoPacket(50);

        if (!pkt)
        {
            if (IsVideoQueueInterrupted())
            {
                // Seek / 退出中：等待队列恢�?
                SDL_Delay(2);

                continue;
            }

            if (demuxEof.load())
            {
                // 队列取空且文件已读完：冲刷解码器剩余�?
                if (!eofFlushed)
                {
                    eofFlushed = true;

                    // 发�?NULL 包触发解码器冲刷
                    SendVideoPacket(nullptr);

                    // 取出所有剩余帧
                    while (true)
                    {
                        FramePtr f;

                        DecodeResult r =
                            ReceiveVideoFrame(f);

                        if (r != DecodeResult::Success)
                        {
                            // NeedMorePacket / End / Error 均停止冲�?
                            break;
                        }

                        // 解码统计（EOF 尾帧同样计数�?
                        if (owner.statistics)
                        {
                            owner.statistics->OnFrameDecoded();
                        }

                        // 输出链（EOF 尾帧同样送编码）
                        owner.FeedOutputVideo(f.get());

                        // 失败�?f 作用域结束自动释�?
                        media.videoFrameQueue.Push(
                            std::move(f),
                            MAX_VIDEO_FRAMES);
                    }
                }

                // 视频解码全部完成
                videoEof.store(true);

                SDL_Delay(2);

                continue;
            }

            // 普通超时（暂时没数据），继续等
            continue;
        }

        if (IsVideoQueueInterrupted())
        {
            // 取到的是 Seek 前的旧包，丢弃（RAII 自动释放�?
            continue;
        }

        // 双检 Seek：取包期间代数可能已变化
        seekGen =
            owner.seekController ?
            owner.seekController->GetGeneration() :
            0;

        if (seekGen != lastSeekGen)
        {
            FlushVideoDecoder();

            lastSeekGen = seekGen;

            eofFlushed = false;

            videoEof.store(false);

            // 丢弃 Seek 前的旧包（RAII 自动释放�?
            continue;
        }

        bool videoDecReady =
            media.videoDecoder != nullptr ||
            (media.hwDecoder &&
                media.hwDecoder->IsReady());

        if (!videoDecReady)
        {
            // 解码器未就绪：丢弃（RAII 自动释放�?
            continue;
        }

        // ---------- 解码 ----------

        // 8.4：send 为同步消费，pkt 用后自动释放
        SendVideoPacket(pkt.get());

        // ---------- 取出所有解码出的帧 ----------

        while (true)
        {
            FramePtr f;

            DecodeResult r =
                ReceiveVideoFrame(f);

            if (r != DecodeResult::Success)
            {
                // NeedMorePacket（需要继续送包�?
                // End（解码结束）/ Error（已记录日志�?
                break;
            }

            // 解码统计（每解出一帧计数一次）
            if (owner.statistics)
            {
                owner.statistics->OnFrameDecoded();
            }

            // 输出链（7.4�?.6）：录制 / 推流 / HLS 共享编码�?
            owner.FeedOutputVideo(f.get());

            if (!media.videoFrameQueue.Push(
                std::move(f),
                MAX_VIDEO_FRAMES))
            {
                // 入队被打断（Seek/退出）：f 自动释放

                // 说明正在 Seek：flush 后等待恢�?
                if (media.videoFrameQueue.IsInterrupted())
                {
                    FlushVideoDecoder();

                    lastSeekGen =
                        owner.seekController ?
                        owner.seekController->GetGeneration() :
                        0;

                    break;
                }
            }

            // 有新数据到达，说明不再是 EOF 状�?
            videoEof.store(false);
        }
    }

    Logger::Info()
        << "[Player] Video thread exit"
        << std::endl;
}
// ============================================================
// Audio 线程主循�?
//
//   1. 检�?Seek 代数变化 -> 清理音频链路（AudioSeekCleanup�?
//   2. �?audioPacketQueue 取包
//   3. 解码 -> 重采�?-> 变�?-> PushPCM
//   4. EOF 冲刷：队列取空且文件读完 -> 冲刷解码器剩余帧
//
//   音频链路完全由本线程独占�?
//   Demux 线程不碰音频（避免跨线程竞争�?
// ============================================================

void PlaybackSession::AudioDecodeLoop()
{
    // Seek 代数：每�?Seek 递增，用于判断是否需要清�?
    int lastSeekGen = 0;

    while (!quit.load())
    {
        // ---------- 检查是否有新的 Seek ----------

        int seekGen =
            owner.seekController ?
            owner.seekController->GetGeneration() :
            0;

        if (seekGen != lastSeekGen)
        {
            lastSeekGen = seekGen;

            AudioSeekCleanup(
                owner.seekController ?
                owner.seekController->GetTarget() :
                0.0);
        }

        // ---------- 取音频包 ----------

        // 8.4：PacketPtr 返回所有权，无需手动释放
        // v2: audio decode is NOT gated on purpose - while audio is
        // paused (SDL stopped) the 3s PCM backpressure blocks PushPCM,
        // so PCM still accumulates during rebuffer and buf=min(net,aud)
        // can reach highWater (gating audio froze PCM -> deadlock).
        PacketPtr pkt =
            PopAudioPacket(50);

        if (!pkt)
        {
            if (IsAudioQueueInterrupted())
            {
                // Seek / 退出中：等待队列恢�?
                SDL_Delay(2);

                continue;
            }

            // EOF 冲刷：队列取空且文件已读�?
            if (demuxEof.load() &&
                !audioEof.load() &&
                media.audioDecoder)
            {
                audioEof.store(true);

                // 发�?NULL 包触发解码器冲刷
                media.audioDecoder->SendPacket(nullptr);

                // 取出所有剩余帧
                FramePtr f;

                while (true)
                {
                    DecodeResult r =
                        media.audioDecoder->ReceiveFrame(f);

                    if (r != DecodeResult::Success)
                    {
                        break;
                    }

                    ProcessAudioFrame(f.get());

                    // 帧已消费，释放引�?
                    f.reset();
                }
            }

            continue;
        }

        if (IsAudioQueueInterrupted() ||
            audioAbort.load())
        {
            // Seek / 退出期间丢弃（RAII 自动释放�?
            continue;
        }

        // 双检 Seek：取包期间代数可能已变化
        seekGen =
            owner.seekController ?
            owner.seekController->GetGeneration() :
            0;

        if (seekGen != lastSeekGen)
        {
            lastSeekGen = seekGen;

            AudioSeekCleanup(
                owner.seekController ?
                owner.seekController->GetTarget() :
                0.0);

            // 丢弃这个包（可能�?Seek 前入队的残留，RAII 自动释放�?
            continue;
        }

        if (!media.audioDecoder ||
            !media.audioResampler ||
            !media.speedController ||
            !media.audioDevice)
        {
            // 音频链未就绪：丢弃（RAII 自动释放�?
            continue;
        }

        // ---------- 解码 ----------

        // 8.4：send 为同步消费，pkt 用后自动释放
        media.audioDecoder->SendPacket(pkt.get());

        // 取出所有解码出�?PCM �?
        FramePtr f;

        while (true)
        {
            DecodeResult r =
                media.audioDecoder->ReceiveFrame(f);

            if (r != DecodeResult::Success)
            {
                break;
            }

            ProcessAudioFrame(f.get());

            // 帧已消费，释放引�?
            f.reset();
        }
    }

    Logger::Info()
        << "[Player] Audio thread exit"
        << std::endl;
}
// ============================================================
// 切换媒体（停线程 -> 释放媒体 -> 重新初始化）
// ============================================================

bool PlaybackSession::SwitchMedia(
    const std::string& path)
{
    Logger::Info()
        << "[Player] SwitchMedia : "
        << path
        << std::endl;

    // 1. 停线�?
    audioAbort.store(true);

    StopThreads();

    // 2. 释放媒体资源
    ReleaseMedia();

    // 3. 重新初始化媒�?
    if (!OpenMedia(path))
    {
        ErrorHandler::Log(
            ErrorTag::Player,
            "SwitchMedia failed : " +
            path);

        return false;
    }

    // reset abort flag: new audio thread must push PCM again
    audioAbort.store(false);

    Logger::Info()
        << "[Player] SwitchMedia Success"
        << std::endl;

    return true;
}
// ============================================================
// 释放媒体资源（保�?SDL 会话与常驻对象）
// ============================================================

void PlaybackSession::ReleaseMedia()
{
    // 输出链（7.4�?.6）：停止录制 / 推流 / HLS 并释�?
    owner.StopAllOutputs();

    // 上一帧副�?
    owner.lastFrame.reset();

    // ---------- 音频链路 ----------

    // 8.1：先解绑音频时钟，避�?MasterClock 持有悬空指针
    if (owner.syncController)
    {
        owner.syncController->SetAudioClock(nullptr);
    }

    if (media.audioDevice)
    {
        media.audioDevice->Close();
    }

    media.audioDevice.reset();

    media.speedController.reset();

    media.audioResampler.reset();

    media.audioDecoder.reset();

    owner.statistics.reset();

    // ---------- SDL 资源 ----------

    if (owner.rgbTexture)
    {
        SDL_DestroyTexture(owner.rgbTexture);

        owner.rgbTexture = nullptr;
    }

    if (owner.texture)
    {
        SDL_DestroyTexture(owner.texture);

        owner.texture = nullptr;
    }

    if (owner.renderer)
    {
        SDL_DestroyRenderer(owner.renderer);

        owner.renderer = nullptr;
    }

    if (owner.window)
    {
        SDL_DestroyWindow(owner.window);

        owner.window = nullptr;
    }

    owner.rgbData.reset();

    owner.rgbLinesize = 0;

    owner.swsCtx.reset();

    owner.swsSrcFmt = AV_PIX_FMT_NONE;

    owner.swsSrcW = 0;

    owner.swsSrcH = 0;

    // OSD 纹理绑定旧渲染器，销毁后重新初始�?
    if (owner.osdManager &&
        owner.fontManager)
    {
        owner.osdManager->Close();

        owner.osdManager->Init(owner.fontManager.get());
    }

    // ---------- 解码�?----------

    media.hwTransferFrame.reset();

    media.hwDecoder.reset();

    media.videoDecoder.reset();

    media.demuxer.reset();

    // ---------- 队列清空 + 复位 ----------

    media.videoPacketQueue.Clear();

    media.audioPacketQueue.Clear();

    media.videoFrameQueue.Clear();

    media.videoPacketQueue.ResetInterrupt();

    media.audioPacketQueue.ResetInterrupt();

    media.videoFrameQueue.ResetInterrupt();

    // 直播队列（NetworkBuffer）同样清�?+ 复位
    media.videoNetBuffer.Clear();

    media.videoNetBuffer.ResetInterrupt();

    // 8.5：音频队�?LiveMode 复位（直�?-> 点播切换时清除状态）
    media.audioPacketQueue.SetLiveMode(
        false,
        1,
        90000,
        500);

    // ---------- 字幕（媒体相关） ----------

    if (owner.subtitleManager)
    {
        owner.subtitleManager->Clear();
    }

    // ---------- 状态复�?----------

    media.duration = 0.0;

    owner.currentTime = 0.0;

    owner.progress = 0.0;

    media.hasAudioStream = false;

    media.videoFrameDuration = 1.0 / 25.0;

    seekPending = false;

    seekPosition = 0.0;

    dropAudioUntil = -1.0;

    videoEof.store(false);

    audioEof.store(false);

    demuxEof.store(false);

    owner.frameStepRequest = false;

    autoAdvancing = false;
}
// ============================================================
// 音频处理（Audio 线程�?
// ============================================================

void PlaybackSession::ProcessAudioFrame(
    AVFrame* frame)
{
    if (!frame)
    {
        return;
    }

    // 惰性初始化重采样器
    if (!media.audioResampler->IsReady())
    {
        if (!media.audioResampler->Init(frame))
        {
            return;
        }
    }

    // 丢弃 Seek 目标之前的旧音频
    if (dropAudioUntil >= 0.0)
    {
        double pts =
            static_cast<double>(
                frame->best_effort_timestamp);

        if (pts != AV_NOPTS_VALUE)
        {
            AVStream* aStream =
                media.demuxer ?
                media.demuxer->GetAudioStream() :
                nullptr;

            if (aStream)
            {
                pts *=
                    av_q2d(aStream->time_base);

                if (pts < dropAudioUntil - 0.05)
                {
                    // 旧音频，丢弃
                    return;
                }
            }
        }

        // 到达目标位置，停止丢�?
        dropAudioUntil = -1.0;
    }

    // 输出链（7.4�?.6）：录制 / 推流 / HLS 共享编码�?
    // （内�?swr 自动转为编码器所需格式，pts 自管理）
    owner.FeedOutputAudio(frame);

    // 重采样为 S16 / 48000Hz / 双声�?
    uint8_t pcmBuffer[192000];

    int samples =
        media.audioResampler->Convert(
            frame,
            pcmBuffer,
            sizeof(pcmBuffer));

    if (samples <= 0)
    {
        return;
    }

    int pcmSize =
        samples *
        media.audioResampler->GetOutputChannels() *
        2;   // S16：每采样 2 字节

    // 变速不变调（speed == 1 时直通）
    uint8_t outBuffer[384000];

    int outSize =
        media.speedController->Process(
            pcmBuffer,
            pcmSize,
            outBuffer,
            sizeof(outBuffer));

    if (outSize > 0)
    {
        media.audioDevice->PushPCM(
            outBuffer,
            outSize,
            &audioAbort);
    }

    // 取完剩余输出
    while ((outSize =
        media.speedController->Flush(
            outBuffer,
            sizeof(outBuffer))) > 0)
    {
        media.audioDevice->PushPCM(
            outBuffer,
            outSize,
            &audioAbort);
    }
}
void PlaybackSession::AudioSeekCleanup(
    double target)
{
    // 清空音频解码器（Seek 后必须，否则解出旧数据）
    if (media.audioDecoder)
    {
        media.audioDecoder->Flush();
    }

    // 清空重采样器内部缓冲
    if (media.audioResampler)
    {
        media.audioResampler->Reset();
    }

    // 清空变速器内部缓冲
    if (media.speedController)
    {
        media.speedController->Reset();
    }

    // 重置音频主时�?+ 清空 PCM 队列
    if (media.audioDevice)
    {
        media.audioDevice->ResetClock(target);

        media.audioDevice->ResetInterrupt();
    }

    // 恢复音频推送（RequestSeek 时置位了 abort�?
    audioAbort.store(false);

    // 丢弃 Seek 目标之前的旧音频�?
    dropAudioUntil = target;

    // 允许新的 EOF 冲刷
    audioEof.store(false);

    Logger::Info()
        << "[Player] Audio seek cleaned : "
        << target
        << " s"
        << std::endl;
}

// ============================================================
// packet queue routing (phase 5.1, moved from Player)
//
//   local / VOD : PacketQueue (blocking backpressure on full)
//   live stream : NetworkBuffer (drop-oldest, latency capped)
//
// Both paths share one entry point so Demux / Video / Audio
// threads do not care which mode is active.
// ============================================================

bool PlaybackSession::PushVideoPacket(
    PacketPtr&& pkt)
{
    if (media.useNetBuffer)
    {
        bool ok =
            media.videoNetBuffer.Push(
                std::move(pkt));

        // live stats: dropped-count delta (a whole GOP may drop at once)
        if (owner.networkStatistics)
        {
            int64_t dropped =
                media.videoNetBuffer.GetDroppedCount();

            int64_t delta =
                dropped - lastVideoDropped;

            if (delta > 0)
            {
                owner.networkStatistics->OnPacketDropped(
                    delta);

                lastVideoDropped = dropped;
            }
        }

        return ok;
    }

    return media.videoPacketQueue.Push(
        std::move(pkt),
        MAX_VIDEO_PACKETS);
}

bool PlaybackSession::PushAudioPacket(
    PacketPtr&& pkt)
{
    // live mode: non-blocking, drops oldest when over live_max_queue_ms
    // VOD: blocking backpressure (same queue object)
    bool ok =
        media.audioPacketQueue.Push(
            std::move(pkt),
            MAX_AUDIO_PACKETS);

    if (owner.networkStatistics)
    {
        int64_t dropped =
            media.audioPacketQueue.GetDroppedCount();

        int64_t delta =
            dropped - lastAudioDropped;

        if (delta > 0)
        {
            owner.networkStatistics->OnPacketDropped(
                delta);

            lastAudioDropped = dropped;
        }
    }

    return ok;
}

PacketPtr PlaybackSession::PopVideoPacket(
    int timeoutMs)
{
    if (media.useNetBuffer)
    {
        return media.videoNetBuffer.Pop(timeoutMs);
    }

    return media.videoPacketQueue.Pop(timeoutMs);
}

PacketPtr PlaybackSession::PopAudioPacket(
    int timeoutMs)
{
    // live and VOD share the same PacketQueue (LiveMode handles drops)
    return media.audioPacketQueue.Pop(timeoutMs);
}

bool PlaybackSession::IsVideoQueueInterrupted() const
{
    if (media.useNetBuffer)
    {
        return media.videoNetBuffer.IsInterrupted();
    }

    return media.videoPacketQueue.IsInterrupted();
}

bool PlaybackSession::IsAudioQueueInterrupted() const
{
    return media.audioPacketQueue.IsInterrupted();
}

int PlaybackSession::GetVideoQueueSize() const
{
    if (media.useNetBuffer)
    {
        return media.videoNetBuffer.Size();
    }

    return media.videoPacketQueue.Size();
}

int PlaybackSession::GetAudioQueueSize() const
{
    return media.audioPacketQueue.Size();
}

int PlaybackSession::GetVideoQueueCapacity() const
{
    if (media.useNetBuffer)
    {
        return media.videoNetBuffer.GetMaxSize();
    }

    return MAX_VIDEO_PACKETS;
}

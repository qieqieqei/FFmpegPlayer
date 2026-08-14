#include "Player.h"

#include "Renderer.h"
#include "Event.h"
#include "Utils/ErrorHandler.h"
#include "Utils/Logger.h"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <vector>

#include "Network/StreamMonitor.h"
#include "Hardware/CUDAContext.h"

Player::Player()
{
}

Player::~Player()
{
    Close();
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

// ============================================================
// 配置加载�?.11�?
//
// 读取 exe 目录 / 当前目录下的 player.json �?stream.json�?
// 文件缺失时使用内置默认值（不报错）�?
// 应在 Init 之前调用；可多次调用（重新加载）�?
// ============================================================

bool Player::LoadConfig()
{
    // 常驻：仅创建一�?
    if (!configManager)
    {
        configManager =
            std::make_unique<ConfigManager>();
    }

    if (!configManager->Load("."))
    {
        Logger::Warn()
            << "[Player] Config load failed, use defaults"
            << std::endl;
    }

    const PlayerConfig& cfg =
        configManager->GetPlayerConfig();

    // ---------- 应用配置 ----------

    // 播放速度�?.5 ~ 2.0�?
    if (cfg.playbackSpeed > 0.1 &&
        cfg.playbackSpeed <= 2.0)
    {
        playbackSpeed =
            cfg.playbackSpeed;
    }

    // 音量�?~100�?
    if (cfg.volume >= 0 &&
        cfg.volume <= 100)
    {
        volume =
            cfg.volume;
    }

    Logger::Info()
        << "[Player] Config loaded : "
        << "speed "
        << playbackSpeed
        << ", volume "
        << volume
        << std::endl;

    return true;
}

ConfigManager* Player::GetConfigManager() const
{
    return configManager.get();
}

// ============================================================
// 初始�?
// ============================================================

bool Player::Init(
    const char* filename)
{
    state = PlayerState::Stopped;

    // ---------- 常驻对象（不随媒体切换销毁） ----------

    // 字体管理�?
    fontManager =
        std::make_unique<FontManager>();

    // Font path: prefer exe dir (association launch may have cwd = video dir)
    std::string fontPath = "Font/simhei.ttf";

    if (char* basePath = SDL_GetBasePath())
    {
        fontPath = std::string(basePath) +
            "Font/simhei.ttf";

        SDL_free(basePath);
    }

    bool fontOk = fontManager->Init(
        fontPath, 24);

    if (!fontOk && fontPath != "Font/simhei.ttf")
    {
        fontOk = fontManager->Init(
            "Font/simhei.ttf", 24);
    }

    if (!fontOk)
    {
        ErrorHandler::Log(
            ErrorTag::Player,
            "FontManager init failed");

        return false;
    }

    // OSD 管理�?
    osdManager =
        std::make_unique<OSDManager>();

    if (!osdManager->Init(
        fontManager.get()))
    {
        ErrorHandler::Log(
            ErrorTag::Player,
            "OSDManager init failed");

        return false;
    }

    // 同步控制�?
    syncController =
        std::make_unique<SyncController>();

    // 截图管理�?
    screenshotManager =
        std::make_unique<ScreenshotManager>();

    // Seek 控制�?
    seekController =
        std::make_unique<SeekController>();

    // 网络流统计（7.2�?
    networkStatistics =
        std::make_unique<NetworkStatistics>();

    // 网络缓冲控制�?.3�?
    bufferController =
        std::make_unique<BufferController>();

    // 流媒体监控（7.9）：网络流健康巡检 + 告警
    streamMonitor =
        std::make_unique<StreamMonitor>();

    if (configManager)
    {
        streamMonitor->Init(
            networkStatistics.get(),
            configManager->GetStreamConfig());
    }

    // 硬件加速探测（7.7）：CUDA -> D3D11VA -> DXVA2�?
    // 失败不影响播放（解码仍走软解�?
    cudaContext =
        std::make_unique<CUDAContext>();

    hardwareReady =
        cudaContext->Init();

    // 字幕管理�?
    subtitleManager =
        std::make_unique<SubtitleManager>();

    // 播放列表管理器（可能已被 AddToPlaylist 提前创建�?
    if (!playlistManager)
    {
        playlistManager =
            std::make_unique<PlaylistManager>();
    }

    // ---------- 打开媒体 ----------

    if (!OpenMedia(filename))
    {
        return false;
    }

    Logger::Info()
        << "[Player] Init Success"
        << std::endl;

    return true;
}

// ============================================================
// 打开媒体（首次初始化 / 播放列表切换共用�?
// ============================================================

bool Player::OpenMedia(
    const std::string& path)
{
    // 记录当前媒体路径（断网重连目标）
    currentMediaPath = path;

    // 复位队列打断状�?
    videoPacketQueue.ResetInterrupt();

    audioPacketQueue.ResetInterrupt();

    videoFrameQueue.ResetInterrupt();

    // ---------- 解复用器 ----------

    demuxer =
        std::make_unique<Demuxer>();

    // 网络参数（rtsp_transport / 超时 / 低延迟，来自 stream.json�?
    if (configManager)
    {
        demuxer->SetNetworkConfig(
            configManager->GetStreamConfig());
    }

    if (!demuxer->Open(path))
    {
        ErrorHandler::Log(
            ErrorTag::Player,
            "Demuxer open failed : " +
            path);

        return false;
    }

    // 网络流：按直�?点播设置缓冲策略�?.3�?
    if (bufferController)
    {
        bufferController->SetLive(
            demuxer->IsLive());
    }

    // 直播流：Demux<->Decode 队列切换 NetworkBuffer（满丢最旧，低延迟）
    // 点播/本地文件：保�?PacketQueue 满阻塞背�?
    useNetBuffer =
        demuxer->IsLive();

    // 8.5：同步策略切直播 / 点播（LiveClock vs DropController�?
    if (syncController)
    {
        syncController->SetLiveMode(
            useNetBuffer);
    }

    if (useNetBuffer)
    {
        int cap = 600;

        int targetMs = 300;

        int liveQueueMs = 500;   // 8.5：直播追最新阈值（默认 500ms�?

        if (configManager)
        {
            const StreamConfig& sc =
                configManager->GetStreamConfig();

            cap = sc.maxBufferPackets;

            targetMs = sc.bufferTargetMs;

            liveQueueMs = sc.liveMaxQueueMs;
        }

        videoNetBuffer.SetMaxSize(cap);

        // 8.5：视频队列时长上限——积压超�?liveQueueMs 丢旧包追最�?
        // （与包数上限叠加；GOP 感知，不撕裂解码链）
        AVStream* liveVStream =
            demuxer->GetVideoStream();

        if (liveVStream)
        {
            videoNetBuffer.SetLiveDurationMs(
                liveQueueMs,
                liveVStream->time_base.num,
                liveVStream->time_base.den);
        }

        // 8.5：音频队列切 LiveMode（PacketQueue 直播模式）：
        // Push 不阻塞，积压超过 liveQueueMs 丢旧包；
        // 音频帧无解码依赖，丢弃安�?
        AVStream* liveAStream =
            demuxer->GetAudioStream();

        if (liveAStream)
        {
            audioPacketQueue.SetLiveMode(
                true,
                liveAStream->time_base.num,
                liveAStream->time_base.den,
                liveQueueMs);
        }
        else
        {
            audioPacketQueue.SetLiveMode(
                true,
                1,
                90000,
                liveQueueMs);
        }

        // 直播目标缓冲（覆盖默�?300ms，按配置�?
        if (bufferController)
        {
            bufferController->SetTargetBufferMs(
                targetMs);
        }

        Logger::Info()
            << "[Player] Live buffer : NetworkBuffer "
            << "cap=" << cap
            << " target=" << targetMs
            << "ms"
            << " liveQueue=" << liveQueueMs
            << "ms (drop old to chase latest)"
            << std::endl;
    }

    if (networkStatistics)
    {
        networkStatistics->Reset();
    }

    // 流媒体监控：切换媒体时复位告�?/ 活性计�?
    if (streamMonitor)
    {
        streamMonitor->Reset();
    }

    if (demuxer->IsNetwork())
    {
        Logger::Info()
            << "[Player] Network stream : "
            << demuxer->GetProtocol()
            << (demuxer->IsLive() ?
                " (live)" :
                " (vod)")
            << std::endl;
    }

    duration =
        demuxer->GetDuration();

    Logger::Info()
        << "[Player] Duration : "
        << duration
        << " s"
        << std::endl;

    AVStream* vStream =
        demuxer->GetVideoStream();

    // ---------- 视频解码�?----------

    // 硬件解码优先（配置开�?+ CUDA 可用 + h264/hevc），
    // 失败自动回退下面的软�?
    TryInitHardwareDecoder(
        vStream->codecpar);

    videoDecoder =
        std::make_unique<VideoDecoder>();

    // 8.5：直播解码级低延迟（avcodec_open2 �?flags=low_delay�?
    videoDecoder->SetLowDelay(
        useNetBuffer);

    if (!videoDecoder->Init(
        vStream->codecpar))
    {
        ErrorHandler::Log(
            ErrorTag::Player,
            "VideoDecoder init failed");

        return false;
    }

    AVCodecContext* vCtx =
        videoDecoder->GetContext();

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

    statistics =
        std::make_unique<PlayerStatistics>();

    statistics->Init(
        demuxer->GetFormatContext(),
        demuxer->GetVideoIndex(),
        demuxer->GetAudioIndex());

    // 无音频时按帧率匀速播�?
    AVRational fpsRat =
        av_guess_frame_rate(
            demuxer->GetFormatContext(),
            vStream,
            nullptr);

    double fps =
        (fpsRat.num > 0 && fpsRat.den > 0) ?
        av_q2d(fpsRat) :
        25.0;

    videoFrameDuration =
        1.0 / fps;

    Logger::Info()
        << "[Player] Video Frame Duration : "
        << videoFrameDuration
        << " s"
        << std::endl;

    // ---------- YUV -> RGB 转换�?----------
    // （惰性创建：GetSwsForFrame 按实际帧格式建，
    //   硬解�?NV12 / 软解�?YUV420P 自动适配�?

    rgbLinesize =
        vCtx->width * 3;

    rgbData =
        std::make_unique<uint8_t[]>(
            rgbLinesize * vCtx->height);

    // ---------- SDL 窗口 / 渲染�?----------

    if (!InitSDL(
        vCtx->width,
        vCtx->height,
        window,
        renderer,
        texture))
    {
        return false;
    }

    rgbTexture =
        SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_RGB24,
            SDL_TEXTUREACCESS_STREAMING,
            vCtx->width,
            vCtx->height);

    if (!rgbTexture)
    {
        ErrorHandler::LogSDL(
            ErrorTag::Player,
            "SDL_CreateTexture (RGB)");

        return false;
    }

    // 切换媒体后恢复全屏状�?
    if (fullscreen)
    {
        SDL_SetWindowFullscreen(
            window,
            SDL_WINDOW_FULLSCREEN_DESKTOP);
    }

    // ---------- 音频链路�?.0 独立 Audio 线程�?----------

    hasAudioStream =
        demuxer->HasAudio();

    if (hasAudioStream)
    {
        // 音频解码�?
        audioDecoder =
            std::make_unique<AudioDecoder>();

        if (!audioDecoder->Init(
            demuxer->GetAudioStream()->codecpar))
        {
            ErrorHandler::Log(
                ErrorTag::Audio,
                "AudioDecoder init failed, video only");

            audioDecoder.reset();

            hasAudioStream = false;
        }
    }

    if (hasAudioStream)
    {
        // SDL 音频设备（与重采样器输出一致：48000Hz / 双声�?/ S16�?
        audioDevice =
            std::make_unique<AudioDevice>();

        if (!audioDevice->Init(
            48000,
            2))
        {
            ErrorHandler::Log(
                ErrorTag::Audio,
                "AudioDevice init failed, video only");

            audioDevice.reset();

            hasAudioStream = false;
        }
    }

    if (hasAudioStream)
    {
        // 重采样器
        audioResampler =
            std::make_unique<AudioResampler>();

        // 变速不变调（SOLA），组合包装�?
        speedController =
            std::make_unique<SpeedController>();

        speedController->Init(
            48000,
            2);

        // 恢复用户设置的速度 / 音量
        speedController->SetSpeed(playbackSpeed);

        audioDevice->SetSpeedFactor(playbackSpeed);

        audioDevice->SetVolume(volume);
    }

    if (!hasAudioStream)
    {
        Logger::Warn()
            << "[Player] Video only mode"
            << std::endl;
    }

    // 8.1：同步控制器绑定音频主时钟（无音频时解绑�?
    // MasterClock 自动回退视频时钟�?
    syncController->SetAudioClock(
        audioDevice ?
        audioDevice->GetClock() :
        nullptr);

    // ---------- Seek 控制器绑�?----------
    // 队列是成员对象（地址不变）；demuxer 每次重建需重新绑定

    seekController->Attach(
        demuxer.get(),
        &videoPacketQueue,
        &audioPacketQueue,
        &videoFrameQueue);

    // ---------- 字幕自动加载（同路径 .srt / .ass�?----------

    if (subtitleManager)
    {
        subtitleManager->Clear();

        std::string base =
            path;

        size_t dot =
            base.find_last_of('.');

        if (dot != std::string::npos)
        {
            base =
                base.substr(0, dot);
        }

        if (!subtitleManager->Load(
            base + ".srt"))
        {
            subtitleManager->Load(
                base + ".ass");
        }
    }

    // ---------- 状态复�?----------

    currentTime = 0.0;

    progress = 0.0;

    seekPending = false;

    seekPosition = 0.0;

    dropAudioUntil = -1.0;

    videoEof.store(false);

    audioEof.store(false);

    demuxEof.store(false);

    frameStepRequest = false;

    autoAdvancing = false;

    return true;
}

// ============================================================
// 渲染主循�?
// ============================================================

bool Player::Run()
{
    // 启动 Demux / Video / Audio 三个线程
    if (!StartThreads())
    {
        return false;
    }

    state = PlayerState::Playing;

    if (audioDevice)
    {
        audioDevice->SetPaused(false);
    }

    Logger::Info()
        << "[Player] State : "
        << StateToString()
        << std::endl;

    bool quit = false;

    while (!quit)
    {
        // ---------- 事件处理 ----------

        HandleEvent(
            quit,
            this);

        // ---------- 播放列表切换请求（`[` / `]`�?----------

        if (switchRequested)
        {
            std::string nextPath =
                switchPath;

            switchRequested = false;

            Logger::Info()
                << "[Player] Switch : "
                << nextPath
                << std::endl;

            if (SwitchMedia(nextPath))
            {
                // 重启三线�?
                if (!StartThreads())
                {
                    quit = true;

                    continue;
                }

                state = PlayerState::Playing;

                if (audioDevice)
                {
                    audioDevice->SetPaused(false);
                }

                autoAdvancing = false;
            }
            else
            {
                ErrorHandler::Log(
                    ErrorTag::Player,
                    "Switch media failed");

                quit = true;
            }

            continue;
        }

        // ---------- 断网重连�?.3）：Demux 线程检测到断流 ----------

        if (reconnectRequested.load())
        {
            reconnectRequested.store(false);

            Logger::Warn()
                << "[Player] Network reconnect : "
                << currentMediaPath
                << std::endl;

            // 循环重试直到成功 / 超限 / 退�?
            // 注意：不�?this->quit——SwitchMedia 内部 StopThreads 会置位它
            bool ok = false;

            while (!ok && !quit)
            {
                // SwitchMedia：停线程 -> 释放 -> 重新打开
                ok = SwitchMedia(currentMediaPath);

                if (ok)
                {
                    if (!StartThreads())
                    {
                        quit = true;

                        break;
                    }

                    state = PlayerState::Playing;

                    if (audioDevice)
                    {
                        audioDevice->SetPaused(false);
                    }

                    reconnectAttempts.store(0);

                    // 重置停滞检测状态，避免误报
                    if (streamMonitor)
                    {
                        streamMonitor->Reset();
                    }

                    Logger::Info()
                        << "[Player] Reconnect success"
                        << std::endl;
                }
                else
                {
                    int attempts =
                        reconnectAttempts
                            .fetch_add(1) +
                        1;

                    StreamConfig cfg =
                        configManager ?
                        configManager->GetStreamConfig() :
                        StreamConfig();

                    int maxAttempts =
                        cfg.reconnectMaxAttempts;

                    int delayMs =
                        cfg.reconnectDelayMs;

                    // 8.4：可选指数退避（factor > 1.0 时开启）�?
                    // delay * factor^(attempts-1)，封�?30s�?
                    // 长时间断网避免高频重试打服务�?
                    if (cfg.reconnectBackoffFactor > 1.0)
                    {
                        double backoff =
                            static_cast<double>(delayMs);

                        for (int i = 1;
                            i < attempts;
                            ++i)
                        {
                            backoff *=
                                cfg.reconnectBackoffFactor;
                        }

                        const double kMaxBackoffMs =
                            30000.0;

                        if (backoff > kMaxBackoffMs)
                        {
                            backoff = kMaxBackoffMs;
                        }

                        delayMs =
                            static_cast<int>(backoff);
                    }

                    // maxAttempts <= 0：无限重试（24h 场景�?
                    if (maxAttempts > 0 &&
                        attempts >= maxAttempts)
                    {
                        ErrorHandler::Log(
                            ErrorTag::Player,
                            "Reconnect failed, give up");

                        // 停止播放，等待用户操�?
                        demuxEof.store(true);

                        break;
                    }

                    Logger::Warn()
                        << "[Player] Reconnect attempt "
                        << attempts
                        << " failed, retry in "
                        << delayMs
                        << " ms"
                        << std::endl;

                    SDL_Delay(delayMs);
                }
            }

            continue;
        }

        // ---------- 字幕更新（查询当前文本） ----------

        if (subtitleManager &&
            osdManager)
        {
            osdManager->SetSubtitle(
                renderer,
                subtitleManager->GetTextAt(
                    GetCurrentTime()));
        }

        // ---------- 取一�?----------

        // 8.4：FramePtr 返回所有权，无需手动释放
        FramePtr frame;

        // Seek 完成：丢弃目标时间之前的旧帧
        if (seekController->IsHandled())
        {
            while ((frame =
                videoFrameQueue.Pop(0)))
            {
                // 旧帧（pts 小于目标）：丢弃（RAII 自动释放�?
                if (GetFramePts(frame.get()) <
                    seekPosition - 0.05)
                {
                    statistics->OnFrameDropped();

                    continue;
                }

                // 到达新位置的第一�?
                break;
            }

            if (frame)
            {
                seekController->ClearHandled();

                seekPending = false;
            }
        }
        else
        {
            // 正常取帧（最多等 10ms�?
            frame =
                videoFrameQueue.Pop(10);
        }

        // ---------- 没有�?----------

        if (!frame)
        {
            // 暂停时（非帧步进）不取帧
            if (state == PlayerState::Paused &&
                !frameStepRequest)
            {
                SDL_Delay(10);

                continue;
            }

            // 帧步进：取一帧后回到暂停
            if (frameStepRequest)
            {
                ClearFrameStepRequest();
            }

            // 视频解码完毕：显示最后一帧，等待用户操作
            if (videoEof.load() &&
                videoFrameQueue.Size() == 0)
            {
                if (state != PlayerState::EndOfFile)
                {
                    state = PlayerState::EndOfFile;

                    Logger::Info()
                        << "[Player] End Of File"
                        << std::endl;

                    // 自动退出计时起点（仅首轮）
                    eofWaitStartMs =
                        static_cast<int64_t>(
                            SDL_GetTicks64());
                }

                // CLI 输出模式�?-record/--hls/--push）：
                // EOF 后停�?3 秒（看最后一帧）再自动退�?
                if (autoQuitOnEof &&
                    eofWaitStartMs >= 0 &&
                    static_cast<int64_t>(
                        SDL_GetTicks64()) -
                        eofWaitStartMs >= 3000)
                {
                    quit = true;

                    continue;
                }


                SDL_Delay(10);

                continue;
            }

            SDL_Delay(2);

            continue;
        }

        // ---------- Seek 期间取到旧帧，丢�?----------

        if (seekPending &&
            GetFramePts(frame.get()) < seekPosition - 0.05)
        {
            statistics->OnFrameDropped();

            // RAII 自动释放
            continue;
        }

        // ---------- 音视频同步（5.1�?----------

        double pts =
            GetFramePts(frame.get());

        // 网络统计：解码出一帧（输入 FPS�?
        if (networkStatistics)
        {
            networkStatistics->OnFrameDecoded();
        }

        bool isStep =
            state == PlayerState::Paused;

        // 8.1：推进视频时钟（无音频时作为主时钟基准）
        syncController->UpdateVideoClock(pts);

        if (HasAudio() &&
            !isStep)
        {
            // 8.4（评审五）：音频墙钟漂移渐进校正�?
            // 约每秒一次（60 �?@60fps），把媒体时间轴向真实时�?
            // 拉回（单�?�?ms 无感知），长期播放进度不漂移
            static int driftTick = 0;

            if (++driftTick >= 60)
            {
                driftTick = 0;

                if (audioDevice)
                {
                    audioDevice->GetClock()->CorrectDrift();
                }
            }

            // MasterClock 自动选择主时钟（音频优先�?
            // 8.4：传帧时长做 ffplay 级目标延迟调整（评审五）
            double delay =
                syncController->GetVideoDelay(
                    pts,
                    videoFrameDuration);

            if (syncController->ShouldDrop(delay))
            {
                // 视频落后：丢帧追赶（DropController 防抖�?
                syncController->OnFrameDropped();

                statistics->OnFrameDropped();

                // RAII 自动释放
                continue;
            }

            // 视频超前：等待（分片等待，保持事件响应）
            // 8.5：直播模式不等待——延迟超过阈值已�?LiveClock 丢帧�?
            //      阈值内的轻微超前直接渲染（追最新，最低延迟）
            while (delay > 0.0 &&
                !quit &&
                !syncController->IsLiveMode())
            {
                HandleEvent(
                    quit,
                    this);

                if (state == PlayerState::Paused)
                {
                    break;
                }

                // Seek 打断等待
                if (seekPending ||
                    seekController->IsHandled())
                {
                    break;
                }

                double chunk =
                    std::min(delay, 0.1);

                SDL_Delay(
                    static_cast<Uint32>(chunk * 1000.0));

                delay -= chunk;
            }

            // 等待期间状态变化：放弃这一帧（RAII 自动释放�?
            if (state == PlayerState::Paused ||
                seekPending ||
                seekController->IsHandled())
            {
                continue;
            }
        }
        else if (!isStep)
        {
            // 无音频：按帧率匀速播�?
            double delay =
                speedController ?
                speedController->GetFrameDelay(
                    videoFrameDuration) :
                videoFrameDuration;

            SDL_Delay(
                static_cast<Uint32>(delay * 1000.0));
        }

        // ---------- 渲染 ----------

        RenderFrame(
            frame.get(),
            window,
            renderer,
            texture,
            this,
            quit);

        statistics->OnFrameRendered();

        // 网络统计：渲染了一帧（输出 FPS�?
        if (networkStatistics)
        {
            networkStatistics->OnFrameRendered();
        }

        // 渲染心跳（Debug 级别：默认不打印�?v 开启）
        Logger::Debug()
            << "[Player] Render frame pts : "
            << pts
            << " s"
            << std::endl;

        // 保存副本供截图（AVFramePtr 自动释放旧帧�?
        lastFrame.reset(
            av_frame_clone(frame.get()));

        // 更新时间 / 进度
        SetCurrentTime(pts);

        // 更新缓冲统计
        UpdateStatistics();

        // 直播缓冲状态跳变提示（7.3）：进入"缓冲�?时一次性打�?
        if (useNetBuffer &&
            bufferController &&
            bufferController->ConsumeBufferingEvent())
        {
            Logger::Info()
                << "[Player] Network buffering..."
                << std::endl;
        }
    }

    // ---------- 退出清�?----------

    Logger::Info()
        << "[Player] Quit loop, stopping threads..."
        << std::endl;

    audioAbort.store(true);

    StopThreads();

    state = PlayerState::Stopped;

    Logger::Info()
        << "[Player] State : "
        << StateToString()
        << std::endl;

    return true;
}

// ============================================================
// 释放资源
// ============================================================

void Player::Close()
{
    // 确保线程先停�?
    audioAbort.store(true);

    StopThreads();

    // 释放媒体资源（窗�?/ 解码�?/ 音频�?/ 队列�?
    ReleaseMedia();

    // ---------- 常驻对象（unique_ptr 自动释放�?----------

    osdManager.reset();

    fontManager.reset();

    syncController.reset();

    screenshotManager.reset();

    seekController.reset();

    networkStatistics.reset();

    bufferController.reset();

    streamMonitor.reset();

    cudaContext.reset();

    hardwareReady = false;

    configManager.reset();

    subtitleManager.reset();

    playlistManager.reset();

    SDL_Quit();

    Logger::Info()
        << "[Player] Closed"
        << std::endl;
}

// ============================================================
// 播放列表�?.8�?
// ============================================================

void Player::AddToPlaylist(
    const std::string& path)
{
    // 播放列表可能�?Init 之前就被填充（main �?Add �?Init），
    // 这里惰性创建，避免依赖 Init 的调用顺�?
    if (!playlistManager)
    {
        playlistManager =
            std::make_unique<PlaylistManager>();
    }

    playlistManager->AddMedia(path);
}

void Player::ExpandPlaylistWithSiblings()
{
    // 8.14 loop: single-file playlist -> scan same folder
    // for sibling videos so EOF auto-advance can cycle
    if (!playlistManager ||
        playlistManager->Count() != 1)
    {
        return;
    }

    const std::string& current =
        playlistManager->GetCurrent();

    // network streams: never scan folders
    if (current.rfind("http://", 0) == 0 ||
        current.rfind("https://", 0) == 0 ||
        current.rfind("rtsp://", 0) == 0 ||
        current.rfind("rtmp://", 0) == 0)
    {
        return;
    }

    std::error_code ec;

    std::filesystem::path dir =
        std::filesystem::path(current)
            .parent_path();

    if (dir.empty())
    {
        return;
    }

    static const char* kVideoExts[] = {
        ".mp4", ".mkv", ".avi", ".mov", ".flv", ".ts",
        ".wmv", ".webm", ".m4v", ".mpg", ".mpeg",
        ".rmvb", ".3gp"
    };

    std::vector<std::string> siblings;

    for (const auto& entry :
        std::filesystem::directory_iterator(
            dir, ec))
    {
        if (ec)
        {
            break;
        }

        if (!entry.is_regular_file(ec))
        {
            continue;
        }

        std::string ext =
            entry.path().extension().string();

        bool isVideo = false;

        for (const char* e : kVideoExts)
        {
            if (_stricmp(ext.c_str(), e) == 0)
            {
                isVideo = true;

                break;
            }
        }

        if (!isVideo)
        {
            continue;
        }

        std::string full =
            entry.path().string();

        if (full == current)
        {
            continue;  // already in list
        }

        siblings.push_back(full);
    }

    if (siblings.empty())
    {
        return;  // keep single file (EOF replays itself)
    }

    std::sort(
        siblings.begin(),
        siblings.end());

    for (const std::string& s : siblings)
    {
        playlistManager->AddMedia(s);
    }

    Logger::Info()
        << "[Player] Expand playlist : "
        << playlistManager->Count()
        << " items"
        << std::endl;
}

bool Player::PlayPrevious()
{
    if (!playlistManager ||
        !playlistManager->Previous())
    {
        return false;
    }

    // 只登记请求，Run 循环里执行切换（避免线程交叉�?
    switchPath =
        playlistManager->GetCurrent();

    switchRequested = true;

    Logger::Info()
        << "[Player] Play previous : "
        << switchPath
        << std::endl;

    return true;
}

bool Player::PlayNext()
{
    if (!playlistManager ||
        !playlistManager->Next())
    {
        return false;
    }

    // 只登记请求，Run 循环里执行切换（避免线程交叉�?
    switchPath =
        playlistManager->GetCurrent();

    switchRequested = true;

    Logger::Info()
        << "[Player] Play next : "
        << switchPath
        << std::endl;

    return true;
}

size_t Player::GetPlaylistIndex() const
{
    return playlistManager ?
        playlistManager->GetIndex() :
        0;
}

size_t Player::GetPlaylistCount() const
{
    return playlistManager ?
        playlistManager->Count() :
        0;
}

const std::string& Player::GetCurrentPath() const
{
    static const std::string empty;

    return playlistManager ?
        playlistManager->GetCurrent() :
        empty;
}

// ============================================================
// 字幕�?.9�?
// ============================================================

void Player::ToggleSubtitle()
{
    if (!subtitleManager)
    {
        return;
    }

    subtitleManager->SetEnabled(
        !subtitleManager->IsEnabled());

    Logger::Info()
        << "[Player] Subtitle : "
        << (subtitleManager->IsEnabled() ?
            "On" : "Off")
        << std::endl;
}

bool Player::IsSubtitleEnabled() const
{
    return subtitleManager ?
        subtitleManager->IsEnabled() :
        false;
}

// ============================================================
// 播放控制
// ============================================================

void Player::RequestSeek(
    double seconds)
{
    // 直播流不�?Seek（RTSP/RTMP/直播 HLS�?
    if (demuxer &&
        !demuxer->IsSeekable())
    {
        Logger::Warn()
            << "[Player] Seek ignored (live stream)"
            << std::endl;

        return;
    }

    // 夹在 [0, 时长] �?
    seconds =
        std::max(
            0.0,
            std::min(
                duration,
                seconds));

    Logger::Info()
        << "[Player] Request Seek : "
        << seconds
        << " s"
        << std::endl;

    // 记录目标（渲染线程丢弃旧帧用�?
    seekPosition = seconds;

    seekPending = true;

    // 让阻塞中�?PushPCM 立即返回（音频线程才能处�?Seek 清理�?
    audioAbort.store(true);

    // 交给 Demux 线程执行
    if (seekController)
    {
        seekController->Request(seconds);
    }
}

void Player::TogglePause()
{
    if (state == PlayerState::Playing)
    {
        Pause();
    }
    else if (state == PlayerState::Paused)
    {
        Resume();
    }
}

void Player::Pause()
{
    if (state == PlayerState::Playing ||
        state == PlayerState::EndOfFile)
    {
        state = PlayerState::Paused;

        // 暂停声卡（队列继续积压，管线自然停止�?
        if (audioDevice)
        {
            audioDevice->SetPaused(true);
        }

        Logger::Info()
            << "[Player] Paused"
            << std::endl;
    }
}

void Player::Resume()
{
    if (state == PlayerState::Paused)
    {
        state = PlayerState::Playing;

        if (audioDevice)
        {
            audioDevice->SetPaused(false);
        }

        Logger::Info()
            << "[Player] Playing"
            << std::endl;
    }
}

PlayerState Player::GetState() const
{
    return state;
}

const char* Player::StateToString() const
{
    switch (state)
    {
    case PlayerState::Stopped:
        return "Stopped";

    case PlayerState::Playing:
        return "Playing";

    case PlayerState::Paused:
        return "Paused";

    case PlayerState::EndOfFile:
        return "EndOfFile";

    default:
        return "Unknown";
    }
}

void Player::RequestFrameStep()
{
    frameStepRequest = true;
}

bool Player::HasFrameStepRequest() const
{
    return frameStepRequest;
}

void Player::ClearFrameStepRequest()
{
    frameStepRequest = false;
}

void Player::SetPlaybackSpeed(
    double speed)
{
    // 支持 0.5x / 1x / 1.5x / 2x
    playbackSpeed = speed;

    // 音频变速不变调（SOLA�?
    if (speedController)
    {
        speedController->SetSpeed(speed);
    }

    // 音频主时钟按速度换算
    if (audioDevice)
    {
        audioDevice->SetSpeedFactor(speed);
    }

    Logger::Info()
        << "[Player] Speed : "
        << playbackSpeed
        << "x"
        << std::endl;
}

double Player::GetPlaybackSpeed() const
{
    return playbackSpeed;
}

void Player::SetVolume(
    int percent)
{
    // 夹在 0~100
    if (percent < 0)
    {
        percent = 0;
    }

    if (percent > 100)
    {
        percent = 100;
    }

    volume = percent;

    if (audioDevice)
    {
        audioDevice->SetVolume(volume);
    }
}

int Player::GetVolume() const
{
    return volume;
}

void Player::TakeScreenshot(
    const std::string& format)
{
    if (!screenshotManager ||
        !lastFrame)
    {
        ErrorHandler::Log(
            ErrorTag::Screenshot,
            "No frame available");

        return;
    }

    screenshotManager->SaveFrame(
        lastFrame.get(),
        format);
}

void Player::ToggleFullScreen()
{
    if (!window)
    {
        return;
    }

    fullscreen = !fullscreen;

    if (fullscreen)
    {
        SDL_SetWindowFullscreen(
            window,
            SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
    else
    {
        SDL_SetWindowFullscreen(
            window,
            0);
    }

    UpdateWindowTitle(
        window,
        this);
}

bool Player::IsFullScreen() const
{
    return fullscreen;
}

const char* Player::FullScreenToString() const
{
    return fullscreen ? "FullScreen" : "Window";
}

// ============================================================
// Seek 状态查�?
// ============================================================

bool Player::HasSeekRequest() const
{
    return seekPending;
}

double Player::GetSeekPosition() const
{
    return seekPosition;
}

bool Player::IsSeekHandled() const
{
    return seekController ?
        seekController->IsHandled() :
        false;
}

void Player::ClearSeekHandled()
{
    if (seekController)
    {
        seekController->ClearHandled();
    }
}

// ============================================================
// 时间 / 进度
// ============================================================

double Player::GetCurrentTime() const
{
    return currentTime;
}

double Player::GetDuration() const
{
    return duration;
}

double Player::GetProgress() const
{
    return progress;
}

std::string Player::GetTimeString() const
{
    int totalMs =
        static_cast<int>(currentTime * 1000.0);

    int hours =
        totalMs / 3600000;

    int minutes =
        (totalMs % 3600000) / 60000;

    int seconds =
        (totalMs % 60000) / 1000;

    int millis =
        totalMs % 1000;

    std::ostringstream oss;

    oss
        << std::setfill('0')
        << std::setw(2)
        << hours
        << ":"
        << std::setw(2)
        << minutes
        << ":"
        << std::setw(2)
        << seconds
        << "."
        << std::setw(3)
        << millis;

    return oss.str();
}

std::string Player::GetDurationString() const
{
    int hour =
        static_cast<int>(duration) / 3600;

    int minute =
        (static_cast<int>(duration) % 3600) / 60;

    int second =
        static_cast<int>(duration) % 60;

    char buffer[32];

    sprintf_s(
        buffer,
        "%02d:%02d:%02d",
        hour,
        minute,
        second);

    return std::string(buffer);
}

void Player::SetCurrentTime(
    double time)
{
    currentTime = time;

    if (duration > 0.0)
    {
        progress =
            currentTime / duration;
    }
    else
    {
        progress = 0.0;
    }
}

// ============================================================
// 渲染 / OSD 访问
// ============================================================

SDL_Window* Player::GetWindow() const
{
    return window;
}

void Player::SetAutoQuitOnEof(
    bool enable)
{
    autoQuitOnEof = enable;
}

int Player::GetVideoWidth() const
{
    if (!videoDecoder ||
        !videoDecoder->GetContext())
    {
        return 0;
    }

    return videoDecoder->GetContext()->width;
}

int Player::GetVideoHeight() const
{
    if (!videoDecoder ||
        !videoDecoder->GetContext())
    {
        return 0;
    }

    return videoDecoder->GetContext()->height;
}

SwsContext* Player::GetSwsForFrame(
    AVFrame* frame)
{
    if (!frame)
    {
        return nullptr;
    }

    AVPixelFormat fmt =
        static_cast<AVPixelFormat>(
            frame->format);

    // 格式 / 尺寸没变：复用现有转换器
    if (swsCtx &&
        swsSrcFmt == fmt &&
        swsSrcW == frame->width &&
        swsSrcH == frame->height)
    {
        return swsCtx.get();
    }

    // 变了（软�?YUV420P <-> 硬解 NV12，或新媒体）：重�?
    swsCtx.reset(
        sws_getContext(
            frame->width,
            frame->height,
            fmt,
            frame->width,
            frame->height,
            AV_PIX_FMT_RGB24,
            SWS_BILINEAR,
            nullptr,
            nullptr,
            nullptr));

    if (swsCtx)
    {
        swsSrcFmt = fmt;

        swsSrcW = frame->width;

        swsSrcH = frame->height;
    }

    return swsCtx.get();}

uint8_t* Player::GetRGBData() const
{
    return rgbData.get();
}

int Player::GetRGBLinesize() const
{
    return rgbLinesize;
}

SDL_Texture* Player::GetRGBTexture() const
{
    return rgbTexture;
}

FontManager* Player::GetFontManager() const
{
    return fontManager.get();
}

OSDManager* Player::GetOSDManager() const
{
    return osdManager.get();
}

PlayerStatistics* Player::GetStatistics() const
{
    return statistics.get();
}

bool Player::IsHardwareDecode() const
{
    return hwDecoder &&
        hwDecoder->IsReady() &&
        hwDecoder->IsHardware();
}

NetworkStatistics* Player::GetNetworkStatistics() const
{
    return networkStatistics.get();
}

void Player::UpdateStatistics()
{
    if (!statistics)
    {
        return;
    }

    statistics->UpdateBuffers(
        GetVideoQueueSize(),           // 视频包缓冲（直播=NetworkBuffer�?
        GetAudioQueueSize(),           // 音频包缓�?
        videoFrameQueue.Size(),            // 视频帧缓�?
        audioDevice ?
            audioDevice->GetQueuedSize() : 0,   // 音频缓冲（字节）
        48000,                             // 音频采样�?
        2);                                // 音频声道

    // 视频帧缓冲时长（毫秒）：帧数 * 帧间�?
    statistics->SetVideoBufferMs(
        static_cast<int>(
            videoFrameQueue.Size() *
            videoFrameDuration *
            1000.0));

    // ---------- 网络缓冲监控�?.2 / 7.3�?----------

    if (!networkStatistics ||
        !bufferController)
    {
        return;
    }

    // 缓冲水位：包�?
    networkStatistics->SetBufferLevel(
        GetVideoQueueSize(),
        GetVideoQueueCapacity());

    // 估算缓冲时长（毫秒）�?
    //   视频：包�?* 帧时�?
    //   音频：缓冲字�?/ (采样�?* 声道 * 2字节)
    double bufferedMs =
        GetVideoQueueSize() *
        videoFrameDuration *
        1000.0;

    if (audioDevice)
    {
        double audioMs =
            audioDevice->GetQueuedSize() *
            1000.0 /
            (48000.0 * 2 * 2);

        if (audioMs > bufferedMs)
        {
            bufferedMs = audioMs;
        }
    }

    // 缓冲控制：水位决策（7.3�?
    bufferController->Update(
        bufferedMs);

    // 延迟估算：近似等于缓冲时长（真实端到端延迟需 RTCP，后续实现）
    networkStatistics->SetLatencyMs(
        static_cast<int>(bufferedMs));

    // 流媒体监控：仅网络流巡检（内部按 1s 节流�?
    if (streamMonitor &&
        demuxer &&
        demuxer->IsNetwork())
    {
        streamMonitor->Tick();
    }
}

// ============================================================
// 直播/点播双路径包队列�?.3�?
//
//   点播/本地文件：PacketQueue（满阻塞背压，防无界内存�?
//   直播流：       NetworkBuffer（满丢最旧包，控制延迟上限）
//
// 统一入口，Demux / Video / Audio 线程不关心当前模式�?
// ============================================================

bool Player::PushVideoPacket(
    PacketPtr&& pkt)
{
    if (useNetBuffer)
    {
        bool ok =
            videoNetBuffer.Push(
                std::move(pkt));

        // 8.4：同步丢包统计（GOP 段丢包可能一次丢多个�?
        if (networkStatistics)
        {
            int64_t dropped =
                videoNetBuffer.GetDroppedCount();

            int64_t delta =
                dropped - lastVideoDropped;

            if (delta > 0)
            {
                networkStatistics->OnPacketDropped(
                    delta);

                lastVideoDropped = dropped;
            }
        }

        return ok;
    }

    return videoPacketQueue.Push(
        std::move(pkt),
        MAX_VIDEO_PACKETS);
}

bool Player::PushAudioPacket(
    PacketPtr&& pkt)
{
    // 8.5：直播时 audioPacketQueue 处于 LiveMode—�?
    // Push 不阻塞，积压超过 live_max_queue_ms 丢旧包；
    // 点播时保持满阻塞背压。两种模式共用一个队列�?
    bool ok =
        audioPacketQueue.Push(
            std::move(pkt),
            MAX_AUDIO_PACKETS);

    // 8.4：同步丢包统计（LiveMode 丢旧包可能一次丢多个�?
    if (networkStatistics)
    {
        int64_t dropped =
            audioPacketQueue.GetDroppedCount();

        int64_t delta =
            dropped - lastAudioDropped;

        if (delta > 0)
        {
            networkStatistics->OnPacketDropped(
                delta);

            lastAudioDropped = dropped;
        }
    }

    return ok;
}

PacketPtr Player::PopVideoPacket(
    int timeoutMs)
{
    if (useNetBuffer)
    {
        return videoNetBuffer.Pop(timeoutMs);
    }

    return videoPacketQueue.Pop(timeoutMs);
}

PacketPtr Player::PopAudioPacket(
    int timeoutMs)
{
    // 8.5：直�?点播统一�?PacketQueue（LiveMode 内部处理丢旧包）
    return audioPacketQueue.Pop(timeoutMs);
}

bool Player::IsVideoQueueInterrupted() const
{
    if (useNetBuffer)
    {
        return videoNetBuffer.IsInterrupted();
    }

    return videoPacketQueue.IsInterrupted();
}

bool Player::IsAudioQueueInterrupted() const
{
    return audioPacketQueue.IsInterrupted();
}

int Player::GetVideoQueueSize() const
{
    if (useNetBuffer)
    {
        return videoNetBuffer.Size();
    }

    return videoPacketQueue.Size();
}

int Player::GetAudioQueueSize() const
{
    return audioPacketQueue.Size();
}

int Player::GetVideoQueueCapacity() const
{
    if (useNetBuffer)
    {
        return videoNetBuffer.GetMaxSize();
    }

    return MAX_VIDEO_PACKETS;
}

// ============================================================
// 输出链：录制 / 推流 / HLS�?.4�?.6 集成�?
//
// 设计：共享编码器 + 多路输出�?
//   一路视频编码器 + 一路音频编码器（按 stream.json 配置），
//   编码出的包同时分发给所有活跃输出（录制文件 / RTMP / HLS），
//   编码只做一次，CPU/GPU 开销最小�?
//
// 线程模型�?
//   Video/Audio 线程每帧加锁调用 FeedOutput*，主线程（事件）
//   加锁调用 Start/Stop —�?outMutex 保证生命周期安全�?
//
// 时间戳：输出�?pts 自管理（视频按帧号、音频按样本数）�?
//   不依赖源流时间基，保证输出流时间戳单调�?
// ============================================================

bool Player::EnsureOutEncoders()
{
    if (!videoDecoder)
    {
        return false;
    }

    if (outVideoEncoder && outAudioEncoder)
    {
        return true;
    }

    StreamConfig cfg =
        configManager ?
        configManager->GetStreamConfig() :
        StreamConfig();

    // ---------- 视频编码�?----------

    if (!outVideoEncoder)
    {
        AVCodecContext* vc =
            videoDecoder->GetContext();

        int fps =
            static_cast<int>(
                videoFrameDuration > 0 ?
                1.0 / videoFrameDuration + 0.5 :
                25.0);

        if (fps <= 0)
        {
            fps = 25;
        }

        outVideoEncoder =
            std::make_unique<VideoEncoder>();

        if (!outVideoEncoder->Init(
            vc->width,
            vc->height,
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
            << " (" << vc->width << "x" << vc->height
            << " @ " << fps << "fps, "
            << cfg.bitrateKbps << "kbps)"
            << std::endl;
    }

    // ---------- 音频编码器（无音频流则输出仅视频�?----------

    if (!outAudioEncoder &&
        demuxer &&
        demuxer->GetAudioStream())
    {
        AVCodecParameters* ap =
            demuxer->GetAudioStream()->codecpar;

        int sr =
            ap->sample_rate > 0 ?
            ap->sample_rate :
            48000;

        int ch =
            ap->ch_layout.nb_channels > 0 ?
            ap->ch_layout.nb_channels :
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

AVFrame* Player::ToYuv420p(
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

void Player::FeedOutputVideo(
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

void Player::FeedOutputAudio(
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

void Player::DispatchVideoPacket(
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

void Player::DispatchAudioPacket(
    AVPacket* pkt)
{
    DispatchVideoPacket(pkt);
}

void Player::FlushOutEncoders()
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

void Player::StopAllOutputs()
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

void Player::ReleaseOutEncoders()
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

// ---------- 录制 ----------

bool Player::StartRecording(
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

void Player::StopRecording()
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

void Player::ToggleRecording()
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

// ---------- 推流 ----------

bool Player::StartPushing(
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
        configManager ?
        configManager->GetStreamConfig() :
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

void Player::StopPushing()
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

void Player::TogglePushing()
{
    if (IsPushing())
    {
        StopPushing();

        return;
    }

    StartPushing("");
}

// ---------- HLS ----------

bool Player::StartHLS(
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
        configManager ?
        configManager->GetStreamConfig() :
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

void Player::StopHLS()
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

void Player::ToggleHLS()
{
    if (IsHLSActive())
    {
        StopHLS();

        return;
    }

    StartHLS("hls_out");
}

// ---------- 状态查�?----------

bool Player::IsRecording() const
{
    return recording;
}

bool Player::IsPushing() const
{
    return pushing;
}

bool Player::IsHLSActive() const
{
    return hlsActive;
}

// ============================================================
// 线程：启�?/ 停止
// ============================================================

bool Player::StartThreads()
{
    quit.store(false);

    demuxThread =
        std::thread(
            &Player::DemuxLoop,
            this);

    videoThread =
        std::thread(
            &Player::VideoDecodeLoop,
            this);

    audioThread =
        std::thread(
            &Player::AudioDecodeLoop,
            this);

    Logger::Info()
        << "[Player] Threads Started (Demux + Video + Audio)"
        << std::endl;

    return true;
}

void Player::StopThreads()
{
    if (!demuxThread.joinable() &&
        !videoThread.joinable() &&
        !audioThread.joinable())
    {
        return;
    }

    quit.store(true);

    // 打断所有阻塞调用，唤醒线程退�?
    videoPacketQueue.Interrupt();

    audioPacketQueue.Interrupt();

    videoFrameQueue.Interrupt();

    // 直播队列（NetworkBuffer）同样打�?
    videoNetBuffer.Interrupt();

    // 打断网络流的阻塞读取（av_read_frame 会立即返回）
    // 否则 RTSP/HTTP 断线或超时时 join 会卡�?
    if (demuxer)
    {
        demuxer->SetAbort(true);
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

void Player::DemuxLoop()
{
    while (!quit.load())
    {
        // ---------- Seek 处理（优先） ----------

        if (seekController &&
            seekController->HasRequest())
        {
            seekController->Execute();

            // Seek 后继续读，不再是 EOF
            demuxEof.store(false);
        }

        if (quit.load())
        {
            break;
        }

        if (!demuxer)
        {
            break;
        }

        // ---------- 读一个包 ----------

        // 8.4：PacketPtr RAII，所有权随包流转（Demux -> 队列 -> 解码器）
        PacketPtr pkt(
            av_packet_alloc());

        int ret =
            demuxer->ReadPacket(pkt.get());

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

            if (useNetBuffer &&
                demuxer &&
                demuxer->IsNetwork())
            {
                Logger::Warn()
                    << "[Player] Network stream error, "
                    << "requesting reconnect"
                    << std::endl;

                reconnectRequested.store(true);

                // 打断阻塞读，退�?Demux 线程（主循环负责重建�?
                demuxer->SetAbort(true);

                break;
            }

            // 短暂等待，让 Seek 请求有机会被处理
            SDL_Delay(2);

            continue;
        }

        // ---------- 分发�?----------

        // 网络统计：收到一个包�?.2�?
        if (networkStatistics)
        {
            networkStatistics->OnPacketReceived(
                pkt->size);
        }

        if (pkt->stream_index ==
            demuxer->GetVideoIndex())
        {
            // 视频包入队（直播：NetworkBuffer 满丢最旧；点播：背压）
            // 8.4：失败时 pkt 仍归本作用域，RAII 自动释放
            PushVideoPacket(std::move(pkt));
        }
        else if (
            pkt->stream_index ==
            demuxer->GetAudioIndex())
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

void Player::FlushVideoDecoder()
{
    if (hwDecoder &&
        hwDecoder->IsReady())
    {
        hwDecoder->Flush();

        return;
    }

    if (videoDecoder)
    {
        videoDecoder->Flush();
    }
}

bool Player::SendVideoPacket(
    AVPacket* pkt)
{
    if (hwDecoder &&
        hwDecoder->IsReady())
    {
        return hwDecoder->SendPacket(pkt);
    }

    return videoDecoder ?
        videoDecoder->SendPacket(pkt) :
        false;
}

DecodeResult Player::ReceiveVideoFrame(
    FramePtr& out)
{
    if (hwDecoder &&
        hwDecoder->IsReady())
    {
        // 硬件路径：先�?GPU 帧，再回读到系统内存（NV12�?
        DecodeResult r =
            hwDecoder->ReceiveFrame(out);

        if (r != DecodeResult::Success)
        {
            return r;
        }

        // 惰性创建回读目标帧
        if (!hwTransferFrame)
        {
            hwTransferFrame.reset(
                av_frame_alloc());

            if (!hwTransferFrame)
            {
                return DecodeResult::Error;
            }
        }

        // GPU �?-> 系统内存（软解模式直�?ref�?
        if (!hwDecoder->TransferFrame(
            out.get(),
            hwTransferFrame.get()))
        {
            return DecodeResult::Error;
        }

        // 回读帧交给调用方（GPU �?out 自动释放�?
        out.reset(
            av_frame_clone(
                hwTransferFrame.get()));

        av_frame_unref(
            hwTransferFrame.get());

        if (!out)
        {
            return DecodeResult::Error;
        }

        return DecodeResult::Success;
    }

    return videoDecoder ?
        videoDecoder->ReceiveFrame(out) :
        DecodeResult::Error;
}

void Player::TryInitHardwareDecoder(
    AVCodecParameters* codecpar)
{
    if (!codecpar ||
        hwDecoder)
    {
        return;
    }

    // 配置开�?
    StreamConfig cfg =
        configManager ?
        configManager->GetStreamConfig() :
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
    if (!cudaContext ||
        !cudaContext->IsAvailable())
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

    hwDecoder =
        std::make_unique<HardwareDecoder>();

    // 8.5：直播解码级低延迟（硬件 + 软解回退两处 avcodec_open2�?
    hwDecoder->SetLowDelay(
        useNetBuffer);

    if (!hwDecoder->Init(
        cudaContext.get(),
        codecName,
        codecpar))
    {
        Logger::Warn()
            << "[Player] Hardware decoder init "
            << "failed, use software"
            << std::endl;

        hwDecoder.reset();
    }
}

void Player::VideoDecodeLoop()
{
    // Seek 代数：每�?Seek 递增，用于判断是否需�?flush
    int lastSeekGen = 0;

    // �?EOF 周期是否已冲刷过解码�?
    bool eofFlushed = false;

    while (!quit.load())
    {
        // ---------- 检查是否有新的 Seek ----------

        int seekGen =
            seekController ?
            seekController->GetGeneration() :
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
                        if (statistics)
                        {
                            statistics->OnFrameDecoded();
                        }

                        // 输出链（EOF 尾帧同样送编码）
                        FeedOutputVideo(f.get());

                        // 失败�?f 作用域结束自动释�?
                        videoFrameQueue.Push(
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
            seekController ?
            seekController->GetGeneration() :
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
            videoDecoder != nullptr ||
            (hwDecoder &&
                hwDecoder->IsReady());

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
            if (statistics)
            {
                statistics->OnFrameDecoded();
            }

            // 输出链（7.4�?.6）：录制 / 推流 / HLS 共享编码�?
            FeedOutputVideo(f.get());

            if (!videoFrameQueue.Push(
                std::move(f),
                MAX_VIDEO_FRAMES))
            {
                // 入队被打断（Seek/退出）：f 自动释放

                // 说明正在 Seek：flush 后等待恢�?
                if (videoFrameQueue.IsInterrupted())
                {
                    FlushVideoDecoder();

                    lastSeekGen =
                        seekController ?
                        seekController->GetGeneration() :
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

void Player::AudioDecodeLoop()
{
    // Seek 代数：每�?Seek 递增，用于判断是否需要清�?
    int lastSeekGen = 0;

    while (!quit.load())
    {
        // ---------- 检查是否有新的 Seek ----------

        int seekGen =
            seekController ?
            seekController->GetGeneration() :
            0;

        if (seekGen != lastSeekGen)
        {
            lastSeekGen = seekGen;

            AudioSeekCleanup(
                seekController ?
                seekController->GetTarget() :
                0.0);
        }

        // ---------- 取音频包 ----------

        // 8.4：PacketPtr 返回所有权，无需手动释放
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
                audioDecoder)
            {
                audioEof.store(true);

                // 发�?NULL 包触发解码器冲刷
                audioDecoder->SendPacket(nullptr);

                // 取出所有剩余帧
                FramePtr f;

                while (true)
                {
                    DecodeResult r =
                        audioDecoder->ReceiveFrame(f);

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
            seekController ?
            seekController->GetGeneration() :
            0;

        if (seekGen != lastSeekGen)
        {
            lastSeekGen = seekGen;

            AudioSeekCleanup(
                seekController ?
                seekController->GetTarget() :
                0.0);

            // 丢弃这个包（可能�?Seek 前入队的残留，RAII 自动释放�?
            continue;
        }

        if (!audioDecoder ||
            !audioResampler ||
            !speedController ||
            !audioDevice)
        {
            // 音频链未就绪：丢弃（RAII 自动释放�?
            continue;
        }

        // ---------- 解码 ----------

        // 8.4：send 为同步消费，pkt 用后自动释放
        audioDecoder->SendPacket(pkt.get());

        // 取出所有解码出�?PCM �?
        FramePtr f;

        while (true)
        {
            DecodeResult r =
                audioDecoder->ReceiveFrame(f);

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

bool Player::SwitchMedia(
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

void Player::ReleaseMedia()
{
    // 输出链（7.4�?.6）：停止录制 / 推流 / HLS 并释�?
    StopAllOutputs();

    // 上一帧副�?
    lastFrame.reset();

    // ---------- 音频链路 ----------

    // 8.1：先解绑音频时钟，避�?MasterClock 持有悬空指针
    if (syncController)
    {
        syncController->SetAudioClock(nullptr);
    }

    if (audioDevice)
    {
        audioDevice->Close();
    }

    audioDevice.reset();

    speedController.reset();

    audioResampler.reset();

    audioDecoder.reset();

    statistics.reset();

    // ---------- SDL 资源 ----------

    if (rgbTexture)
    {
        SDL_DestroyTexture(rgbTexture);

        rgbTexture = nullptr;
    }

    if (texture)
    {
        SDL_DestroyTexture(texture);

        texture = nullptr;
    }

    if (renderer)
    {
        SDL_DestroyRenderer(renderer);

        renderer = nullptr;
    }

    if (window)
    {
        SDL_DestroyWindow(window);

        window = nullptr;
    }

    rgbData.reset();

    rgbLinesize = 0;

    swsCtx.reset();

    swsSrcFmt = AV_PIX_FMT_NONE;

    swsSrcW = 0;

    swsSrcH = 0;

    // OSD 纹理绑定旧渲染器，销毁后重新初始�?
    if (osdManager &&
        fontManager)
    {
        osdManager->Close();

        osdManager->Init(fontManager.get());
    }

    // ---------- 解码�?----------

    hwTransferFrame.reset();

    hwDecoder.reset();

    videoDecoder.reset();

    demuxer.reset();

    // ---------- 队列清空 + 复位 ----------

    videoPacketQueue.Clear();

    audioPacketQueue.Clear();

    videoFrameQueue.Clear();

    videoPacketQueue.ResetInterrupt();

    audioPacketQueue.ResetInterrupt();

    videoFrameQueue.ResetInterrupt();

    // 直播队列（NetworkBuffer）同样清�?+ 复位
    videoNetBuffer.Clear();

    videoNetBuffer.ResetInterrupt();

    // 8.5：音频队�?LiveMode 复位（直�?-> 点播切换时清除状态）
    audioPacketQueue.SetLiveMode(
        false,
        1,
        90000,
        500);

    // ---------- 字幕（媒体相关） ----------

    if (subtitleManager)
    {
        subtitleManager->Clear();
    }

    // ---------- 状态复�?----------

    duration = 0.0;

    currentTime = 0.0;

    progress = 0.0;

    hasAudioStream = false;

    videoFrameDuration = 1.0 / 25.0;

    seekPending = false;

    seekPosition = 0.0;

    dropAudioUntil = -1.0;

    videoEof.store(false);

    audioEof.store(false);

    demuxEof.store(false);

    frameStepRequest = false;

    autoAdvancing = false;
}

// ============================================================
// 音频处理（Audio 线程�?
// ============================================================

void Player::ProcessAudioFrame(
    AVFrame* frame)
{
    if (!frame)
    {
        return;
    }

    // 惰性初始化重采样器
    if (!audioResampler->IsReady())
    {
        if (!audioResampler->Init(frame))
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
                demuxer ?
                demuxer->GetAudioStream() :
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
    FeedOutputAudio(frame);

    // 重采样为 S16 / 48000Hz / 双声�?
    uint8_t pcmBuffer[192000];

    int samples =
        audioResampler->Convert(
            frame,
            pcmBuffer,
            sizeof(pcmBuffer));

    if (samples <= 0)
    {
        return;
    }

    int pcmSize =
        samples *
        audioResampler->GetOutputChannels() *
        2;   // S16：每采样 2 字节

    // 变速不变调（speed == 1 时直通）
    uint8_t outBuffer[384000];

    int outSize =
        speedController->Process(
            pcmBuffer,
            pcmSize,
            outBuffer,
            sizeof(outBuffer));

    if (outSize > 0)
    {
        audioDevice->PushPCM(
            outBuffer,
            outSize,
            &audioAbort);
    }

    // 取完剩余输出
    while ((outSize =
        speedController->Flush(
            outBuffer,
            sizeof(outBuffer))) > 0)
    {
        audioDevice->PushPCM(
            outBuffer,
            outSize,
            &audioAbort);
    }
}

void Player::AudioSeekCleanup(
    double target)
{
    // 清空音频解码器（Seek 后必须，否则解出旧数据）
    if (audioDecoder)
    {
        audioDecoder->Flush();
    }

    // 清空重采样器内部缓冲
    if (audioResampler)
    {
        audioResampler->Reset();
    }

    // 清空变速器内部缓冲
    if (speedController)
    {
        speedController->Reset();
    }

    // 重置音频主时�?+ 清空 PCM 队列
    if (audioDevice)
    {
        audioDevice->ResetClock(target);

        audioDevice->ResetInterrupt();
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
// 工具
// ============================================================

double Player::GetFramePts(
    AVFrame* frame) const
{
    if (!frame)
    {
        return 0.0;
    }

    // 优先�?best_effort_timestamp
    int64_t ts =
        frame->best_effort_timestamp;

    if (ts == AV_NOPTS_VALUE)
    {
        ts = frame->pts;
    }

    if (ts == AV_NOPTS_VALUE)
    {
        return 0.0;
    }

    AVStream* vStream =
        demuxer ?
        demuxer->GetVideoStream() :
        nullptr;

    if (!vStream)
    {
        return 0.0;
    }

    return ts * av_q2d(vStream->time_base);
}

bool Player::HasAudio() const
{
    return hasAudioStream &&
        audioDevice != nullptr;
}

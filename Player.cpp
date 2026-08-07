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

#include "Network/StreamMonitor.h"
#include "Hardware/CUDAContext.h"

Player::Player()
{
}

Player::~Player()
{
    Close();
}

// ============================================================
// 配置加载（7.11）
//
// 读取 exe 目录 / 当前目录下的 player.json 与 stream.json，
// 文件缺失时使用内置默认值（不报错）。
// 应在 Init 之前调用；可多次调用（重新加载）。
// ============================================================

bool Player::LoadConfig()
{
    // 常驻：仅创建一次
    if (!configManager)
    {
        configManager =
            new ConfigManager();
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

    // 播放速度（0.5 ~ 2.0）
    if (cfg.playbackSpeed > 0.1 &&
        cfg.playbackSpeed <= 2.0)
    {
        playbackSpeed =
            cfg.playbackSpeed;
    }

    // 音量（0~100）
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
    return configManager;
}

// ============================================================
// 初始化
// ============================================================

bool Player::Init(
    const char* filename)
{
    state = PlayerState::Stopped;

    // ---------- 常驻对象（不随媒体切换销毁） ----------

    // 字体管理器
    fontManager =
        new FontManager();

    if (!fontManager->Init(
        "Font/simhei.ttf",
        24))
    {
        ErrorHandler::Log(
            ErrorTag::Player,
            "FontManager init failed");

        return false;
    }

    // OSD 管理器
    osdManager =
        new OSDManager();

    if (!osdManager->Init(
        fontManager))
    {
        ErrorHandler::Log(
            ErrorTag::Player,
            "OSDManager init failed");

        return false;
    }

    // 同步控制器
    syncController =
        new SyncController();

    // 截图管理器
    screenshotManager =
        new ScreenshotManager();

    // Seek 控制器
    seekController =
        new SeekController();

    // 网络流统计（7.2）
    networkStatistics =
        new NetworkStatistics();

    // 网络缓冲控制（7.3）
    bufferController =
        new BufferController();

    // 流媒体监控（7.9）：网络流健康巡检 + 告警
    streamMonitor =
        new StreamMonitor();

    if (configManager)
    {
        streamMonitor->Init(
            networkStatistics,
            configManager->GetStreamConfig());
    }

    // 硬件加速探测（7.7）：CUDA -> D3D11VA -> DXVA2，
    // 失败不影响播放（解码仍走软解）
    cudaContext =
        new CUDAContext();

    hardwareReady =
        cudaContext->Init();

    // 字幕管理器
    subtitleManager =
        new SubtitleManager();

    // 播放列表管理器（可能已被 AddToPlaylist 提前创建）
    if (!playlistManager)
    {
        playlistManager =
            new PlaylistManager();
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
// 打开媒体（首次初始化 / 播放列表切换共用）
// ============================================================

bool Player::OpenMedia(
    const std::string& path)
{
    // 复位队列打断状态
    videoPacketQueue.ResetInterrupt();

    audioPacketQueue.ResetInterrupt();

    videoFrameQueue.ResetInterrupt();

    // ---------- 解复用器 ----------

    demuxer =
        new Demuxer();

    // 网络参数（rtsp_transport / 超时 / 低延迟，来自 stream.json）
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

    // 网络流：按直播/点播设置缓冲策略（7.3）
    if (bufferController)
    {
        bufferController->SetLive(
            demuxer->IsLive());
    }

    if (networkStatistics)
    {
        networkStatistics->Reset();
    }

    // 流媒体监控：切换媒体时复位告警 / 活性计时
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

    // ---------- 视频解码器 ----------

    videoDecoder =
        new VideoDecoder();

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

    // ---------- 播放统计（5.7） ----------

    statistics =
        new PlayerStatistics();

    statistics->Init(
        demuxer->GetFormatContext(),
        demuxer->GetVideoIndex(),
        demuxer->GetAudioIndex());

    // 无音频时按帧率匀速播放
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

    // ---------- YUV -> RGB 转换器 ----------

    swsCtx =
        sws_getContext(
            vCtx->width,
            vCtx->height,
            vCtx->pix_fmt,
            vCtx->width,
            vCtx->height,
            AV_PIX_FMT_RGB24,
            SWS_BILINEAR,
            nullptr,
            nullptr,
            nullptr);

    if (!swsCtx)
    {
        ErrorHandler::Log(
            ErrorTag::Player,
            "sws_getContext failed");

        return false;
    }

    rgbLinesize =
        vCtx->width * 3;

    rgbData =
        new uint8_t[
            rgbLinesize * vCtx->height];

    // ---------- SDL 窗口 / 渲染器 ----------

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

    // 切换媒体后恢复全屏状态
    if (fullscreen)
    {
        SDL_SetWindowFullscreen(
            window,
            SDL_WINDOW_FULLSCREEN_DESKTOP);
    }

    // ---------- 音频链路（6.0 独立 Audio 线程） ----------

    hasAudioStream =
        demuxer->HasAudio();

    if (hasAudioStream)
    {
        // 音频解码器
        audioDecoder =
            new AudioDecoder();

        if (!audioDecoder->Init(
            demuxer->GetAudioStream()->codecpar))
        {
            ErrorHandler::Log(
                ErrorTag::Audio,
                "AudioDecoder init failed, video only");

            delete audioDecoder;

            audioDecoder = nullptr;

            hasAudioStream = false;
        }
    }

    if (hasAudioStream)
    {
        // SDL 音频设备（与重采样器输出一致：48000Hz / 双声道 / S16）
        audioDevice =
            new AudioDevice();

        if (!audioDevice->Init(
            48000,
            2))
        {
            ErrorHandler::Log(
                ErrorTag::Audio,
                "AudioDevice init failed, video only");

            delete audioDevice;

            audioDevice = nullptr;

            hasAudioStream = false;
        }
    }

    if (hasAudioStream)
    {
        // 重采样器
        audioResampler =
            new AudioResampler();

        // 变速不变调（SOLA），组合包装类
        speedController =
            new SpeedController();

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

    // ---------- Seek 控制器绑定 ----------
    // 队列是成员对象（地址不变）；demuxer 每次重建需重新绑定

    seekController->Attach(
        demuxer,
        &videoPacketQueue,
        &audioPacketQueue,
        &videoFrameQueue);

    // ---------- 字幕自动加载（同路径 .srt / .ass） ----------

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

    // ---------- 状态复位 ----------

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
// 渲染主循环
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

        // ---------- 播放列表切换请求（`[` / `]`） ----------

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
                // 重启三线程
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

        // ---------- 字幕更新（查询当前文本） ----------

        if (subtitleManager &&
            osdManager)
        {
            osdManager->SetSubtitle(
                renderer,
                subtitleManager->GetTextAt(
                    GetCurrentTime()));
        }

        // ---------- 取一帧 ----------

        AVFrame* frame = nullptr;

        // Seek 完成：丢弃目标时间之前的旧帧
        if (seekController->IsHandled())
        {
            while ((frame =
                videoFrameQueue.Pop(0)) != nullptr)
            {
                // 旧帧（pts 小于目标）：丢弃
                if (GetFramePts(frame) <
                    seekPosition - 0.05)
                {
                    statistics->OnFrameDropped();

                    av_frame_free(&frame);

                    continue;
                }

                // 到达新位置的第一帧
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
            // 正常取帧（最多等 10ms）
            frame =
                videoFrameQueue.Pop(10);
        }

        // ---------- 没有帧 ----------

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
                }

                // 自动播放：切下一首
                if (playlistManager &&
                    playlistManager->IsAutoPlay() &&
                    !autoAdvancing &&
                    playlistManager->Count() > 1)
                {
                    autoAdvancing = true;

                    playlistManager->Next();

                    Logger::Info()
                        << "[Player] Auto play next : "
                        << playlistManager->GetCurrent()
                        << std::endl;

                    if (SwitchMedia(
                        playlistManager->GetCurrent()))
                    {
                        // 重启三线程
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
                        Logger::Info()
                            << "[Player] Auto play switch failed"
                            << std::endl;
                    }
                }

                SDL_Delay(10);

                continue;
            }

            SDL_Delay(2);

            continue;
        }

        // ---------- Seek 期间取到旧帧，丢弃 ----------

        if (seekPending &&
            GetFramePts(frame) < seekPosition - 0.05)
        {
            statistics->OnFrameDropped();

            av_frame_free(&frame);

            continue;
        }

        // ---------- 音视频同步（5.1） ----------

        double pts =
            GetFramePts(frame);

        // 网络统计：解码出一帧（输入 FPS）
        if (networkStatistics)
        {
            networkStatistics->OnFrameDecoded();
        }

        bool isStep =
            state == PlayerState::Paused;

        if (HasAudio() &&
            !isStep)
        {
            // 音频主时钟：delay = 视频pts - 音频时钟
            double delay =
                syncController->GetVideoDelay(
                    pts,
                    audioDevice->GetAudioClock());

            if (delay <
                -syncController->GetDropThreshold())
            {
                // 视频落后太多：丢帧追赶
                statistics->OnFrameDropped();

                av_frame_free(&frame);

                continue;
            }

            // 视频超前：等待（分片等待，保持事件响应）
            while (delay > 0.0 &&
                !quit)
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

            // 等待期间状态变化：放弃这一帧
            if (state == PlayerState::Paused ||
                seekPending ||
                seekController->IsHandled())
            {
                av_frame_free(&frame);

                continue;
            }
        }
        else if (!isStep)
        {
            // 无音频：按帧率匀速播放
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
            frame,
            window,
            renderer,
            texture,
            this,
            quit);

        statistics->OnFrameRendered();

        // 网络统计：渲染了一帧（输出 FPS）
        if (networkStatistics)
        {
            networkStatistics->OnFrameRendered();
        }

        // 渲染心跳（Debug 级别：默认不打印，-v 开启）
        Logger::Debug()
            << "[Player] Render frame pts : "
            << pts
            << " s"
            << std::endl;

        // 保存副本供截图
        if (lastFrame)
        {
            av_frame_free(&lastFrame);
        }

        lastFrame =
            av_frame_clone(frame);

        av_frame_free(&frame);

        // 更新时间 / 进度
        SetCurrentTime(pts);

        // 更新缓冲统计
        UpdateStatistics();
    }

    // ---------- 退出清理 ----------

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
    // 确保线程先停止
    audioAbort.store(true);

    StopThreads();

    // 释放媒体资源（窗口 / 解码器 / 音频链 / 队列）
    ReleaseMedia();

    // ---------- 常驻对象 ----------

    if (osdManager)
    {
        delete osdManager;

        osdManager = nullptr;
    }

    if (fontManager)
    {
        delete fontManager;

        fontManager = nullptr;
    }

    if (syncController)
    {
        delete syncController;

        syncController = nullptr;
    }

    if (screenshotManager)
    {
        delete screenshotManager;

        screenshotManager = nullptr;
    }

    if (seekController)
    {
        delete seekController;

        seekController = nullptr;
    }

    if (networkStatistics)
    {
        delete networkStatistics;

        networkStatistics = nullptr;
    }

    if (bufferController)
    {
        delete bufferController;

        bufferController = nullptr;
    }

    if (streamMonitor)
    {
        delete streamMonitor;

        streamMonitor = nullptr;
    }

    if (cudaContext)
    {
        delete cudaContext;

        cudaContext = nullptr;
    }

    hardwareReady = false;

    if (configManager)
    {
        delete configManager;

        configManager = nullptr;
    }

    if (subtitleManager)
    {
        delete subtitleManager;

        subtitleManager = nullptr;
    }

    if (playlistManager)
    {
        delete playlistManager;

        playlistManager = nullptr;
    }

    SDL_Quit();

    Logger::Info()
        << "[Player] Closed"
        << std::endl;
}

// ============================================================
// 播放列表（6.8）
// ============================================================

void Player::AddToPlaylist(
    const std::string& path)
{
    // 播放列表可能在 Init 之前就被填充（main 先 Add 后 Init），
    // 这里惰性创建，避免依赖 Init 的调用顺序
    if (!playlistManager)
    {
        playlistManager =
            new PlaylistManager();
    }

    playlistManager->AddMedia(path);
}

bool Player::PlayPrevious()
{
    if (!playlistManager ||
        !playlistManager->Previous())
    {
        return false;
    }

    // 只登记请求，Run 循环里执行切换（避免线程交叉）
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

    // 只登记请求，Run 循环里执行切换（避免线程交叉）
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
// 字幕（6.9）
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
    // 直播流不可 Seek（RTSP/RTMP/直播 HLS）
    if (demuxer &&
        !demuxer->IsSeekable())
    {
        Logger::Warn()
            << "[Player] Seek ignored (live stream)"
            << std::endl;

        return;
    }

    // 夹在 [0, 时长] 内
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

    // 记录目标（渲染线程丢弃旧帧用）
    seekPosition = seconds;

    seekPending = true;

    // 让阻塞中的 PushPCM 立即返回（音频线程才能处理 Seek 清理）
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

        // 暂停声卡（队列继续积压，管线自然停止）
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

    // 音频变速不变调（SOLA）
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
        lastFrame,
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
// Seek 状态查询
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

SwsContext* Player::GetSwsContext() const
{
    return swsCtx;
}

uint8_t* Player::GetRGBData() const
{
    return rgbData;
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
    return fontManager;
}

OSDManager* Player::GetOSDManager() const
{
    return osdManager;
}

PlayerStatistics* Player::GetStatistics() const
{
    return statistics;
}

void Player::UpdateStatistics()
{
    if (!statistics)
    {
        return;
    }

    statistics->UpdateBuffers(
        videoPacketQueue.Size(),           // 视频包缓冲
        audioPacketQueue.Size(),           // 音频包缓冲
        videoFrameQueue.Size(),            // 视频帧缓冲
        audioDevice ?
            audioDevice->GetQueuedSize() : 0,   // 音频缓冲（字节）
        48000,                             // 音频采样率
        2);                                // 音频声道

    // ---------- 网络缓冲监控（7.2 / 7.3） ----------

    if (!networkStatistics ||
        !bufferController)
    {
        return;
    }

    // 缓冲水位：包数
    networkStatistics->SetBufferLevel(
        videoPacketQueue.Size(),
        MAX_VIDEO_PACKETS);

    // 估算缓冲时长（毫秒）：
    //   视频：包数 * 帧时长
    //   音频：缓冲字节 / (采样率 * 声道 * 2字节)
    double bufferedMs =
        videoPacketQueue.Size() *
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

    // 缓冲控制：水位决策（7.3）
    bufferController->Update(
        bufferedMs);

    // 延迟估算：近似等于缓冲时长（真实端到端延迟需 RTCP，后续实现）
    networkStatistics->SetLatencyMs(
        static_cast<int>(bufferedMs));

    // 流媒体监控：仅网络流巡检（内部按 1s 节流）
    if (streamMonitor &&
        demuxer &&
        demuxer->IsNetwork())
    {
        streamMonitor->Tick();
    }
}

// ============================================================
// 线程：启动 / 停止
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

    // 打断所有阻塞调用，唤醒线程退出
    videoPacketQueue.Interrupt();

    audioPacketQueue.Interrupt();

    videoFrameQueue.Interrupt();

    // 打断网络流的阻塞读取（av_read_frame 会立即返回）
    // 否则 RTSP/HTTP 断线或超时时 join 会卡死
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
// Demux 线程主循环
//
//   1. 处理 Seek 请求（优先）
//   2. av_read_frame 读一个包
//   3. 视频包 -> videoPacketQueue
//   4. 音频包 -> audioPacketQueue
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

        AVPacket* pkt =
            av_packet_alloc();

        int ret =
            demuxer->ReadPacket(pkt);

        if (ret < 0)
        {
            av_packet_free(&pkt);

            if (ret == AVERROR_EOF)
            {
                // 文件读完了：标记 EOF（音频线程负责冲刷解码器）
                demuxEof.store(true);
            }
            else
            {
                ErrorHandler::LogFFmpeg(
                    ErrorTag::Player,
                    "av_read_frame",
                    ret);
            }

            // 短暂等待，让 Seek 请求有机会被处理
            SDL_Delay(2);

            continue;
        }

        // ---------- 分发包 ----------

        // 网络统计：收到一个包（7.2）
        if (networkStatistics)
        {
            networkStatistics->OnPacketReceived(
                pkt->size);
        }

        if (pkt->stream_index ==
            demuxer->GetVideoIndex())
        {
            // 视频包入队（Video 线程消费）
            if (!videoPacketQueue.Push(
                pkt,
                MAX_VIDEO_PACKETS))
            {
                // 入队被打断（Seek/退出），自行释放
                av_packet_free(&pkt);
            }
        }
        else if (
            pkt->stream_index ==
            demuxer->GetAudioIndex())
        {
            // 音频包入队（Audio 线程消费）
            if (!audioPacketQueue.Push(
                pkt,
                MAX_AUDIO_PACKETS))
            {
                av_packet_free(&pkt);
            }
        }
        else
        {
            // 其他流（字幕等）直接丢弃
            av_packet_free(&pkt);
        }
    }

    Logger::Info()
        << "[Player] Demux thread exit"
        << std::endl;
}

// ============================================================
// Video 线程主循环
//
//   1. 从 videoPacketQueue 取包
//   2. avcodec_send_packet + avcodec_receive_frame
//   3. 帧入 videoFrameQueue（Render 线程消费）
//
//   Seek 时队列会被 Interrupt：
//   - 立即 flush 视频解码器（丢弃 Seek 前的解码状态）
//   - 等待队列恢复后继续
// ============================================================

void Player::VideoDecodeLoop()
{
    // Seek 代数：每次 Seek 递增，用于判断是否需要 flush
    int lastSeekGen = 0;

    // 本 EOF 周期是否已冲刷过解码器
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
            // 新的一次 Seek：清空解码器内部状态
            if (videoDecoder)
            {
                videoDecoder->Flush();
            }

            lastSeekGen = seekGen;

            eofFlushed = false;

            // 解码未结束
            videoEof.store(false);
        }

        // ---------- 取视频包 ----------

        AVPacket* pkt =
            videoPacketQueue.Pop(50);

        if (!pkt)
        {
            if (videoPacketQueue.IsInterrupted())
            {
                // Seek / 退出中：等待队列恢复
                SDL_Delay(2);

                continue;
            }

            if (demuxEof.load())
            {
                // 队列取空且文件已读完：冲刷解码器剩余帧
                if (!eofFlushed &&
                    videoDecoder)
                {
                    eofFlushed = true;

                    // 发送 NULL 包触发解码器冲刷
                    videoDecoder->SendPacket(nullptr);

                    // 取出所有剩余帧
                    while (true)
                    {
                        AVFrame* f =
                            videoDecoder->ReceiveFrame();

                        if (!f)
                        {
                            break;
                        }

                        // 克隆一帧入队（内部帧会被复用）
                        AVFrame* out =
                            av_frame_clone(f);

                        av_frame_unref(f);

                        if (!out)
                        {
                            break;
                        }

                        if (!videoFrameQueue.Push(
                            out,
                            MAX_VIDEO_FRAMES))
                        {
                            // 入队被打断，释放
                            av_frame_free(&out);
                        }
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

        if (videoPacketQueue.IsInterrupted())
        {
            // 取到的是 Seek 前的旧包，丢弃
            av_packet_free(&pkt);

            continue;
        }

        // 双检 Seek：取包期间代数可能已变化
        seekGen =
            seekController ?
            seekController->GetGeneration() :
            0;

        if (seekGen != lastSeekGen)
        {
            if (videoDecoder)
            {
                videoDecoder->Flush();
            }

            lastSeekGen = seekGen;

            eofFlushed = false;

            videoEof.store(false);

            av_packet_free(&pkt);

            continue;
        }

        if (!videoDecoder)
        {
            av_packet_free(&pkt);

            continue;
        }

        // ---------- 解码 ----------

        videoDecoder->SendPacket(pkt);

        av_packet_free(&pkt);

        // ---------- 取出所有解码出的帧 ----------

        while (true)
        {
            AVFrame* f =
                videoDecoder->ReceiveFrame();

            if (!f)
            {
                // 需要更多包，或解码结束
                break;
            }

            // 克隆一帧入队（内部帧会被复用）
            AVFrame* out =
                av_frame_clone(f);

            av_frame_unref(f);

            if (!out)
            {
                break;
            }

            if (!videoFrameQueue.Push(
                out,
                MAX_VIDEO_FRAMES))
            {
                // 入队被打断（Seek/退出）
                av_frame_free(&out);

                // 说明正在 Seek：flush 后等待恢复
                if (videoFrameQueue.IsInterrupted())
                {
                    if (videoDecoder)
                    {
                        videoDecoder->Flush();
                    }

                    lastSeekGen =
                        seekController ?
                        seekController->GetGeneration() :
                        0;

                    break;
                }
            }

            // 有新数据到达，说明不再是 EOF 状态
            videoEof.store(false);
        }
    }

    Logger::Info()
        << "[Player] Video thread exit"
        << std::endl;
}

// ============================================================
// Audio 线程主循环
//
//   1. 检查 Seek 代数变化 -> 清理音频链路（AudioSeekCleanup）
//   2. 从 audioPacketQueue 取包
//   3. 解码 -> 重采样 -> 变速 -> PushPCM
//   4. EOF 冲刷：队列取空且文件读完 -> 冲刷解码器剩余帧
//
//   音频链路完全由本线程独占，
//   Demux 线程不碰音频（避免跨线程竞争）
// ============================================================

void Player::AudioDecodeLoop()
{
    // Seek 代数：每次 Seek 递增，用于判断是否需要清理
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

        AVPacket* pkt =
            audioPacketQueue.Pop(50);

        if (!pkt)
        {
            if (audioPacketQueue.IsInterrupted())
            {
                // Seek / 退出中：等待队列恢复
                SDL_Delay(2);

                continue;
            }

            // EOF 冲刷：队列取空且文件已读完
            if (demuxEof.load() &&
                !audioEof.load() &&
                audioDecoder)
            {
                audioEof.store(true);

                // 发送 NULL 包触发解码器冲刷
                audioDecoder->SendPacket(nullptr);

                // 取出所有剩余帧
                AVFrame* f = nullptr;

                while ((f =
                    audioDecoder->ReceiveFrame()) != nullptr)
                {
                    ProcessAudioFrame(f);
                }
            }

            continue;
        }

        if (audioPacketQueue.IsInterrupted() ||
            audioAbort.load())
        {
            // Seek / 退出期间丢弃
            av_packet_free(&pkt);

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

            // 丢弃这个包（可能是 Seek 前入队的残留）
            av_packet_free(&pkt);

            continue;
        }

        if (!audioDecoder ||
            !audioResampler ||
            !speedController ||
            !audioDevice)
        {
            av_packet_free(&pkt);

            continue;
        }

        // ---------- 解码 ----------

        audioDecoder->SendPacket(pkt);

        av_packet_free(&pkt);

        // 取出所有解码出的 PCM 帧
        AVFrame* f = nullptr;

        while ((f =
            audioDecoder->ReceiveFrame()) != nullptr)
        {
            ProcessAudioFrame(f);
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

    // 1. 停线程
    audioAbort.store(true);

    StopThreads();

    // 2. 释放媒体资源
    ReleaseMedia();

    // 3. 重新初始化媒体
    if (!OpenMedia(path))
    {
        ErrorHandler::Log(
            ErrorTag::Player,
            "SwitchMedia failed : " +
            path);

        return false;
    }

    Logger::Info()
        << "[Player] SwitchMedia Success"
        << std::endl;

    return true;
}

// ============================================================
// 释放媒体资源（保留 SDL 会话与常驻对象）
// ============================================================

void Player::ReleaseMedia()
{
    // 上一帧副本
    if (lastFrame)
    {
        av_frame_free(&lastFrame);

        lastFrame = nullptr;
    }

    // ---------- 音频链路 ----------

    if (audioDevice)
    {
        audioDevice->Close();

        delete audioDevice;

        audioDevice = nullptr;
    }

    if (speedController)
    {
        delete speedController;

        speedController = nullptr;
    }

    if (audioResampler)
    {
        delete audioResampler;

        audioResampler = nullptr;
    }

    if (audioDecoder)
    {
        delete audioDecoder;

        audioDecoder = nullptr;
    }

    if (statistics)
    {
        delete statistics;

        statistics = nullptr;
    }

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

    if (rgbData)
    {
        delete[] rgbData;

        rgbData = nullptr;

        rgbLinesize = 0;
    }

    if (swsCtx)
    {
        sws_freeContext(swsCtx);

        swsCtx = nullptr;
    }

    // OSD 纹理绑定旧渲染器，销毁后重新初始化
    if (osdManager &&
        fontManager)
    {
        osdManager->Close();

        osdManager->Init(fontManager);
    }

    // ---------- 解码器 ----------

    if (videoDecoder)
    {
        delete videoDecoder;

        videoDecoder = nullptr;
    }

    if (demuxer)
    {
        delete demuxer;

        demuxer = nullptr;
    }

    // ---------- 队列清空 + 复位 ----------

    videoPacketQueue.Clear();

    audioPacketQueue.Clear();

    videoFrameQueue.Clear();

    videoPacketQueue.ResetInterrupt();

    audioPacketQueue.ResetInterrupt();

    videoFrameQueue.ResetInterrupt();

    // ---------- 字幕（媒体相关） ----------

    if (subtitleManager)
    {
        subtitleManager->Clear();
    }

    // ---------- 状态复位 ----------

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
// 音频处理（Audio 线程）
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

        // 到达目标位置，停止丢弃
        dropAudioUntil = -1.0;
    }

    // 重采样为 S16 / 48000Hz / 双声道
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

    // 重置音频主时钟 + 清空 PCM 队列
    if (audioDevice)
    {
        audioDevice->ResetClock(target);

        audioDevice->ResetInterrupt();
    }

    // 恢复音频推送（RequestSeek 时置位了 abort）
    audioAbort.store(false);

    // 丢弃 Seek 目标之前的旧音频帧
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

    // 优先用 best_effort_timestamp
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

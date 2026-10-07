#include "core/Player.h"

#include "output/video/Renderer.h"
#include "app/Event.h"
#include "infra/ErrorHandler.h"
#include "infra/Logger.h"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <vector>

#include "streaming/StreamMonitor.h"
#include "hardware/CUDAContext.h"

Player::Player()
{
    media = std::make_unique<MediaContext>();
    session = std::make_unique<PlaybackSession>(*this, *media);
}

Player::~Player()
{
    Close();
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

    if (!output)
    {
        output =
            std::make_unique<OutputPipeline>(
                configManager.get());
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

void Player::SetLiveBufferOverride(
    int ms)
{
    liveBufferOverrideMs = ms;
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

    if (!session->OpenMedia(filename))
    {
        return false;
    }

    Logger::Info()
        << "[Player] Init Success"
        << std::endl;

    return true;
}


// ============================================================
// 渲染主循�?
// ============================================================

bool Player::Run()
{
    // 启动 Demux / Video / Audio 三个线程
    if (!session->StartThreads())
    {
        return false;
    }

    state = PlayerState::Playing;

    // v2: live pre-buffer - keep audio paused until watermark reached
    // (gate in render loop releases it once via state-change, not per-frame)
    if (media->audioDevice &&
        !(media->useNetBuffer &&
            bufferController &&
            bufferController->IsConsumingBlocked()))
    {
        media->audioDevice->SetPaused(false);
    }

    Logger::Info()
        << "[Player] State : "
        << StateToString()
        << std::endl;

    bool quit = false;

    // v2: buffer gate state latch (only act on transitions)
    bool lastBufferingBlock = false;

    while (!quit)
    {
        // ---------- 事件处理 ----------

        HandleEvent(
            quit,
            this);

        // ---------- 播放列表切换请求（`[` / `]`�?----------

        if (session->switchRequested)
        {
            session->HandleSwitchRequest(quit);

            continue;
        }

        // ---------- 断网重连�?.3）：Demux 线程检测到断流 ----------

        if (session->reconnectRequested.load())
        {
            session->HandleReconnect(quit);

            continue;
        }

        // ---------- 字幕更新（查询当前文本） ----------

        if (subtitleManager &&
            osdManager)
        {
            osdManager->SetSubtitle(
                media->presenter.GetRenderer(),
                subtitleManager->GetTextAt(
                    GetCurrentTime()));
        }

        // ---------- frame acquisition + A/V sync (phase 5.3b: moved to PlaybackSession::AcquireAndSyncFrame) ----------

        FramePtr frame;
        double pts = 0.0;

        if (session->AcquireAndSyncFrame(
                frame,
                pts,
                quit,
                lastBufferingBlock) ==
            PlaybackSession::FrameAction::Skip)
        {
            continue;
        }

        // render (phase 5.3: moved to PlaybackSession::PresentFrame)

        session->PresentFrame(
            frame.get(),
            pts,
            quit,
            lastBufferingBlock);
    }

    // ---------- 退出清�?----------

    Logger::Info()
        << "[Player] Quit loop, stopping threads..."
        << std::endl;

    session->audioAbort.store(true);

    session->StopThreads();

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
    session->audioAbort.store(true);

    session->StopThreads();

    // 释放媒体资源（窗�?/ 解码�?/ 音频�?/ 队列�?
    session->ReleaseMedia();

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
    session->AddToPlaylist(path);
}

void Player::ExpandPlaylistWithSiblings()
{
    session->ExpandPlaylistWithSiblings();
}

bool Player::PlayPrevious()
{
    return session->PlayPrevious();
}

bool Player::PlayNext()
{
    return session->PlayNext();
}

size_t Player::GetPlaylistIndex() const
{
    return session->GetPlaylistIndex();
}

size_t Player::GetPlaylistCount() const
{
    return session->GetPlaylistCount();
}

const std::string& Player::GetCurrentPath() const
{
    return session->GetCurrentPath();
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
    session->RequestSeek(seconds);
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
        if (media->audioDevice)
        {
            media->audioDevice->SetPaused(true);
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

        if (media->audioDevice)
        {
            media->audioDevice->SetPaused(false);
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
    if (media->speedController)
    {
        media->speedController->SetSpeed(speed);
    }

    // 音频主时钟按速度换算
    if (media->audioDevice)
    {
        media->audioDevice->SetSpeedFactor(speed);
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

    if (media->audioDevice)
    {
        media->audioDevice->SetVolume(volume);
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
        !media->presenter.GetLastFrame())
    {
        ErrorHandler::Log(
            ErrorTag::Screenshot,
            "No frame available");

        return;
    }

    screenshotManager->SaveFrame(
        media->presenter.GetLastFrame(),
        format);
}

void Player::ToggleFullScreen()
{
    if (!media->presenter.GetWindow())
    {
        return;
    }

    fullscreen = !fullscreen;

    if (fullscreen)
    {
        media->presenter.ApplyFullscreen(true);
    }
    else
    {
        media->presenter.ApplyFullscreen(false);
    }

    UpdateWindowTitle(
        media->presenter.GetWindow(),
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
    return session->HasSeekRequest();
}

double Player::GetSeekPosition() const
{
    return session->GetSeekPosition();
}

bool Player::IsSeekHandled() const
{
    return session->IsSeekHandled();
}

void Player::ClearSeekHandled()
{
    session->ClearSeekHandled();
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
    return media->duration;
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
        static_cast<int>(media->duration) / 3600;

    int minute =
        (static_cast<int>(media->duration) % 3600) / 60;

    int second =
        static_cast<int>(media->duration) % 60;

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

    if (media->duration > 0.0)
    {
        progress =
            currentTime / media->duration;
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
    return media->presenter.GetWindow();
}

void Player::SetAutoQuitOnEof(
    bool enable)
{
    autoQuitOnEof = enable;
}

int Player::GetVideoWidth() const
{
    if (!media->videoDecoder ||
        !media->videoDecoder->GetContext())
    {
        return 0;
    }

    return media->videoDecoder->GetContext()->width;
}

int Player::GetVideoHeight() const
{
    if (!media->videoDecoder ||
        !media->videoDecoder->GetContext())
    {
        return 0;
    }

    return media->videoDecoder->GetContext()->height;
}

SwsContext* Player::GetSwsForFrame(
    AVFrame* frame)
{
    return media->presenter.GetSwsForFrame(frame);
}

uint8_t* Player::GetRGBData() const
{
    return media->presenter.GetRGBData();
}

int Player::GetRGBLinesize() const
{
    return media->presenter.GetRGBLinesize();
}

SDL_Texture* Player::GetRGBTexture() const
{
    return media->presenter.GetRGBTexture();
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
    return media->hwDecoder &&
        media->hwDecoder->IsReady() &&
        media->hwDecoder->IsHardware();
}

NetworkStatistics* Player::GetNetworkStatistics() const
{
    return networkStatistics.get();
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

// ---------- 录制 ----------

bool Player::StartRecording(
    const std::string& path)
{
    return output->StartRecording(path);
}

void Player::StopRecording()
{
    output->StopRecording();
}

void Player::ToggleRecording()
{
    output->ToggleRecording();
}

// ---------- 推流 ----------

bool Player::StartPushing(
    const std::string& url)
{
    return output->StartPushing(url);
}

void Player::StopPushing()
{
    output->StopPushing();
}

void Player::TogglePushing()
{
    output->TogglePushing();
}

// ---------- HLS ----------

bool Player::StartHLS(
    const std::string& dir)
{
    return output->StartHLS(dir);
}

void Player::StopHLS()
{
    output->StopHLS();
}

void Player::ToggleHLS()
{
    output->ToggleHLS();
}

// ---------- 状态查�?----------

bool Player::IsRecording() const
{
    return output->IsRecording();
}

bool Player::IsPushing() const
{
    return output->IsPushing();
}

bool Player::IsHLSActive() const
{
    return output->IsHLSActive();
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
        media->demuxer ?
        media->demuxer->GetVideoStream() :
        nullptr;

    if (!vStream)
    {
        return 0.0;
    }

    return ts * av_q2d(vStream->time_base);
}

bool Player::HasAudio() const
{
    return media->hasAudioStream &&
        media->audioDevice != nullptr;
}

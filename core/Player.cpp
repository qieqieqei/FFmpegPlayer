#include "core/Player.h"

#include "output/video/Renderer.h"
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
    return session->Prepare(
        filename);
}


// ============================================================
// 渲染主循�?
// ============================================================

void Player::SetScreenshotAt(
    double seconds,
    const std::string& path)
{
    screenshotAtSec =
        seconds;

    screenshotAtDone =
        false;

    if (path.empty())
    {
        std::ostringstream oss;

        oss << "screenshot_at_"
            << seconds
            << "s.bmp";

        screenshotAtPath =
            oss.str();
    }
    else
    {
        screenshotAtPath =
            path;
    }

    Logger::Info()
        << "[Player] Auto screenshot at "
        << seconds
        << " s : "
        << screenshotAtPath
        << std::endl;
}

void Player::SetInputHandler(
    IInputHandler* handler)
{
    inputHandler = handler;
}

bool Player::Run()
{
    return session->RunLoop();
}

// ============================================================
// 释放资源
// ============================================================

void Player::Close()
{
    session->Shutdown();

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

    RenderContext renderCtx =
        session->BuildRenderContext(nullptr);

    UpdateWindowTitle(
        renderCtx);
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

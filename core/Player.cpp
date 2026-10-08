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
    return session->ApplyConfig();
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
    session->ConfigureScreenshot(
        seconds,
        path);
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
    session->ToggleSubtitleEnabled();
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
    session->TogglePausePlayback();
}

void Player::Pause()
{
    session->PausePlayback();
}

void Player::Resume()
{
    session->ResumePlayback();
}

PlayerState Player::GetState() const
{
    return state;
}

const char* Player::StateToString() const
{
    return session->StateToString();
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
    session->ApplySpeed(speed);
}

double Player::GetPlaybackSpeed() const
{
    return playbackSpeed;
}

void Player::SetVolume(
    int percent)
{
    session->ApplyVolume(percent);
}

int Player::GetVolume() const
{
    return volume;
}

void Player::TakeScreenshot(
    const std::string& format)
{
    session->CaptureScreenshot(format);
}

void Player::ToggleFullScreen()
{
    session->ToggleFullScreenMode();
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
    return session->FormatTimeString();
}

std::string Player::GetDurationString() const
{
    return session->FormatDurationString();
}

void Player::SetCurrentTime(
    double time)
{
    session->SetPlaybackTime(time);
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
    return session->GetVideoWidth();
}

int Player::GetVideoHeight() const
{
    return session->GetVideoHeight();
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
    return session->IsHardwareDecode();
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
    return session->GetFramePts(frame);
}

bool Player::HasAudio() const
{
    return media->hasAudioStream &&
        media->audioDevice != nullptr;
}

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
            std::string nextPath =
                session->switchPath;

            session->switchRequested = false;

            Logger::Info()
                << "[Player] Switch : "
                << nextPath
                << std::endl;

            if (session->SwitchMedia(nextPath))
            {
                // 重启三线�?
                if (!session->StartThreads())
                {
                    quit = true;

                    continue;
                }

                state = PlayerState::Playing;

                // v2: pre-buffer gate (see Run())
                if (media->audioDevice &&
                    !(media->useNetBuffer &&
                        bufferController &&
                        bufferController->IsConsumingBlocked()))
                {
                    media->audioDevice->SetPaused(false);
                }

                session->autoAdvancing = false;
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

        if (session->reconnectRequested.load())
        {
            session->reconnectRequested.store(false);

            Logger::Warn()
                << "[Player] Network reconnect : "
                << session->currentMediaPath
                << std::endl;

            // 循环重试直到成功 / 超限 / 退�?
            // 注意：不�?this->quit——SwitchMedia 内部 StopThreads 会置位它
            bool ok = false;

            while (!ok && !quit)
            {
                // SwitchMedia：停线程 -> 释放 -> 重新打开
                ok = session->SwitchMedia(session->currentMediaPath);

                if (ok)
                {
                    if (!session->StartThreads())
                    {
                        quit = true;

                        break;
                    }

                    state = PlayerState::Playing;

                    // v2: pre-buffer gate (see Run())
                    if (media->audioDevice &&
                        !(media->useNetBuffer &&
                            bufferController &&
                            bufferController->IsConsumingBlocked()))
                    {
                        media->audioDevice->SetPaused(false);
                    }

                    session->reconnectAttempts.store(0);

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
                        session->reconnectAttempts
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
                        session->demuxEof.store(true);

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
                media->videoFrameQueue.Pop(0)))
            {
                // 旧帧（pts 小于目标）：丢弃（RAII 自动释放�?
                if (GetFramePts(frame.get()) <
                    session->seekPosition - 0.05)
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

                session->seekPending = false;
            }
        }
        else
        {
            // 正常取帧（最多等 10ms�?
            frame =
                media->videoFrameQueue.Pop(10);
        }

        // ---------- 没有�?----------

        if (!frame)
        {
            // v2: buffer gate - consuming blocked (prebuffer/rebuffer/stall),
            // frame queue drained by design, idle until released.
            // NOTE: must still advance the state machine here, otherwise the
            // render loop never reaches UpdateStatistics() and the controller
            // stays in Prebuffering forever (deadlock: gated decode threads
            // never produce frames, no frame -> no Update -> no release).
            if (media->useNetBuffer &&
                bufferController &&
                bufferController->IsConsumingBlocked())
            {
                static Uint32 lastGateLog = 0;

                Uint32 gNow = SDL_GetTicks();

                if (gNow - lastGateLog >= 500)
                {
                    lastGateLog = gNow;

                    Logger::Info()
                        << "[Gate] render gate state="
                        << bufferController->GetStateName()
                        << " buf="
                        << static_cast<int>(bufferController->GetBufferedMs())
                        << std::endl;
                }

                if (!lastBufferingBlock)
                {
                    lastBufferingBlock = true;

                    if (media->audioDevice)
                    {
                        media->audioDevice->SetPaused(true);
                    }

                    media->videoNetBuffer.SetDurationTrimEnabled(false);

                    Logger::Info()
                        << "[Player] Buffer gate : HOLD state="
                        << bufferController->GetStateName()
                        << " aclk="
                        << (media->audioDevice ?
                            media->audioDevice->GetAudioClock() : -1.0)
                        << " vclk="
                        << syncController->GetVideoClockTime()
                        << std::endl;
                }

                UpdateStatistics();

                SDL_Delay(10);

                continue;
            }
            else if (lastBufferingBlock)
            {
                // v2: release edge reached via idle path - rebuffer ended
                // but the frame queue is still empty (decode threads were
                // just released). Re-anchor the audio clock to the oldest
                // buffered video pts so playback resumes from the buffer
                // point instead of waiting for the frozen clock to catch
                // up (that wait freezes the render loop for seconds).
                lastBufferingBlock = false;

                double anchorPts =
                    media->videoNetBuffer.GetFrontPts();

                if (media->audioDevice)
                {
                    media->audioDevice->ResetClock(
                        anchorPts >= 0.0 ? anchorPts : 0.0);
                }

                media->videoNetBuffer.SetDurationTrimEnabled(true);

                if (media->audioDevice)
                {
                    media->audioDevice->SetPaused(false);
                }

                Logger::Info()
                    << "[Player] Buffer gate : RELEASE state="
                    << (bufferController ?
                        bufferController->GetStateName() : "?")
                    << " aclk="
                    << (media->audioDevice ?
                        media->audioDevice->GetAudioClock() : -1.0)
                    << " vclk="
                    << syncController->GetVideoClockTime()
                    << std::endl;
            }

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
            if (session->videoEof.load() &&
                media->videoFrameQueue.Size() == 0)
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

            static Uint32 lastIdleLog = 0;

            Uint32 iNow = SDL_GetTicks();

            if (iNow - lastIdleLog >= 500)
            {
                lastIdleLog = iNow;

                Logger::Info()
                    << "[Gate] render idle state="
                    << (bufferController ?
                        bufferController->GetStateName() : "?")
                    << " q="
                    << media->videoFrameQueue.Size()
                    << std::endl;
            }

            SDL_Delay(2);

            continue;
        }

        // ---------- Seek 期间取到旧帧，丢�?----------

        if (session->seekPending &&
            GetFramePts(frame.get()) < session->seekPosition - 0.05)
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

                if (media->audioDevice)
                {
                    media->audioDevice->GetClock()->CorrectDrift();
                }
            }

            // MasterClock 自动选择主时钟（音频优先�?
            // 8.4：传帧时长做 ffplay 级目标延迟调整（评审五）
            double delay =
                syncController->GetVideoDelay(
                    pts,
                    media->videoFrameDuration);

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
                delay <= 2.0 &&
                !quit &&
                (!syncController->IsLiveMode() ||
                    media->useNetBuffer))
            {
                HandleEvent(
                    quit,
                    this);

                if (state == PlayerState::Paused)
                {
                    break;
                }

                // Seek 打断等待
                if (session->seekPending ||
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
                session->seekPending ||
                seekController->IsHandled())
            {
                continue;
            }
        }
        else if (!isStep)
        {
            // 无音频：按帧率匀速播�?
            double delay =
                media->speedController ?
                media->speedController->GetFrameDelay(
                    media->videoFrameDuration) :
                media->videoFrameDuration;

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
        // v2: buffer gate state sync (transition-driven, no per-frame toggle)
        bool bufferingBlock =
            media->useNetBuffer &&
            bufferController &&
            bufferController->IsConsumingBlocked();

        if (bufferingBlock != lastBufferingBlock)
        {
            lastBufferingBlock = bufferingBlock;

            if (!bufferingBlock && media->audioDevice)
            {
                // v2: release edge via render path - re-anchor audio clock
                // to the frame just rendered (skip frozen-clock catch-up).
                media->audioDevice->ResetClock(pts);
            }

            if (media->audioDevice)
            {
                media->audioDevice->SetPaused(bufferingBlock);
            }

            media->videoNetBuffer.SetDurationTrimEnabled(!bufferingBlock);

            Logger::Info()
                << "[Player] Buffer gate : "
                << (bufferingBlock ? "HOLD" : "RELEASE")
                << " state="
                << (bufferController ? bufferController->GetStateName() : "?")
                << " aclk="
                << (media->audioDevice ?
                    media->audioDevice->GetAudioClock() : -1.0)
                << " vclk="
                << syncController->GetVideoClockTime()
                << std::endl;
        }
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
    session->switchPath =
        playlistManager->GetCurrent();

    session->switchRequested = true;

    Logger::Info()
        << "[Player] Play previous : "
        << session->switchPath
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
    session->switchPath =
        playlistManager->GetCurrent();

    session->switchRequested = true;

    Logger::Info()
        << "[Player] Play next : "
        << session->switchPath
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
    if (media->demuxer &&
        !media->demuxer->IsSeekable())
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
                media->duration,
                seconds));

    Logger::Info()
        << "[Player] Request Seek : "
        << seconds
        << " s"
        << std::endl;

    // 记录目标（渲染线程丢弃旧帧用�?
    session->seekPosition = seconds;

    session->seekPending = true;

    // 让阻塞中�?PushPCM 立即返回（音频线程才能处�?Seek 清理�?
    session->audioAbort.store(true);

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
    return session->seekPending;
}

double Player::GetSeekPosition() const
{
    return session->seekPosition;
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
    return window;
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
    return media->hwDecoder &&
        media->hwDecoder->IsReady() &&
        media->hwDecoder->IsHardware();
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
        session->GetVideoQueueSize(),           // 视频包缓冲（直播=NetworkBuffer�?
        session->GetAudioQueueSize(),           // 音频包缓�?
        media->videoFrameQueue.Size(),            // 视频帧缓�?
        media->audioDevice ?
            media->audioDevice->GetQueuedSize() : 0,   // 音频缓冲（字节）
        48000,                             // 音频采样�?
        2);                                // 音频声道

    // 视频帧缓冲时长（毫秒）：帧数 * 帧间�?
    statistics->SetVideoBufferMs(
        static_cast<int>(
            media->videoFrameQueue.Size() *
            media->videoFrameDuration *
            1000.0));

    // ---------- 网络缓冲监控�?.2 / 7.3�?----------

    if (!networkStatistics ||
        !bufferController)
    {
        return;
    }

    // 缓冲水位：包�?
    networkStatistics->SetBufferLevel(
        session->GetVideoQueueSize(),
        session->GetVideoQueueCapacity());

    // 估算缓冲时长（毫秒）�?
    //   视频：包�?* 帧时�?
    //   音频：缓冲字�?/ (采样�?* 声道 * 2字节)
    // v2: unified bufferedMs - min(video, audio)
    //   video: live = NetworkBuffer::GetDurationMs() (PTS/time_base)
    //          vod   = packet count * frame duration (legacy)
    //   audio: PCM bytes / (actual sample_rate * channels * 2)
    double videoMs;

    // v2 Metrics: audio buffer ms at function scope
    double audioMs = 0.0;

    if (media->useNetBuffer)
    {
        videoMs =
            static_cast<double>(
                media->videoNetBuffer.GetDurationMs());
    }
    else
    {
        videoMs =
            session->GetVideoQueueSize() *
            media->videoFrameDuration *
            1000.0;
    }

    double bufferedMs = videoMs;

    if (media->audioDevice)
    {
        int sr = media->audioDevice->GetSampleRate();

        int ch = media->audioDevice->GetChannels();

        double bytesPerSec =
            (sr > 0 && ch > 0) ?
            sr * ch * 2.0 :
            0.0;

        double aclkSec =
            media->audioDevice->GetAudioClock();

        double backPtsSec =
            media->useNetBuffer ?
            media->videoNetBuffer.GetBackPts() :
            -1.0;

        if (media->useNetBuffer && backPtsSec > 0.0 && aclkSec > 0.0)
        {
            // v2: live audio buffer depth = how far the audio clock
            // lags the push head (audio delayed by same backlog as
            // video). PCM queue alone is a poor gauge in PLAYING:
            // it stays near zero (instant FIFO) which drags
            // buf=min(video,audio) to ~0 and forces rebuffer loops.
            double lagMs =
                (backPtsSec - aclkSec) * 1000.0;

            audioMs = lagMs > 0.0 ? lagMs : 0.0;
        }
        else
        {
            audioMs =
                (bytesPerSec > 0.0) ?
                media->audioDevice->GetQueuedSize() *
                    1000.0 / bytesPerSec :
                0.0;
        }

        // min watermark: both sides must be ready before release;
        // when audio is absent/empty (no audio stream or drained after
        // stall), fall back to video to avoid deadlock where audioMs=0
        // prevents reaching highWater forever
        if (audioMs > 0.0)
        {
            bufferedMs =
                videoMs < audioMs ?
                videoMs :
                audioMs;
        }
        else
        {
            bufferedMs = videoMs;
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
        media->demuxer &&
        media->demuxer->IsNetwork())
    {
        streamMonitor->Tick();
    }

    // ---------- v2 Metrics锛氭瘡绉掍竴琛?----------
    {
        static Uint32 lastMetricsTick = 0;

        Uint32 nowTick = SDL_GetTicks();

        if (nowTick - lastMetricsTick >= 1000)
        {
            lastMetricsTick = nowTick;

            const int netMs =
                media->videoNetBuffer.GetDurationMs();

            const int avSyncMs =
                syncController ?
                syncController->GetAvSyncMs() : 0;

            const int latMs =
                networkStatistics ?
                networkStatistics->GetLatencyMs() : 0;

            Logger::Info()
                << "[Metrics] state="
                << (bufferController ?
                    bufferController->GetStateName() : "?")
                << " net=" << netMs << "ms"
                << " vid=" << static_cast<int>(videoMs) << "ms"
                << " aud=" << static_cast<int>(audioMs) << "ms"
                << " buf=" << static_cast<int>(bufferedMs) << "ms"
                << " lat=" << latMs << "ms"
                << " avsync=" << avSyncMs << "ms"
                << " stall="
                << (bufferController ?
                    bufferController->GetStallCount() : 0)
                << " underrun="
                << (bufferController ?
                    bufferController->GetUnderrunCount() : 0)
                << " bufCnt="
                << (bufferController ?
                    bufferController->GetBufferingCount() : 0)
                << "/"
                << (bufferController ?
                    bufferController->GetBufferingDurationMs() : 0)
                << "ms"
                << " dropPkt="
                << (networkStatistics ?
                    networkStatistics->GetDroppedPackets() : 0)
                << " dropFrame="
                << (statistics ?
                    statistics->GetDroppedFrames() : 0)
                << " lateDrop="
                << (syncController ?
                    syncController->GetDropCount() : 0)
                << " fps="
                << (statistics ? statistics->GetDecodeFPS() : 0.0)
                << "/"
                << (statistics ? statistics->GetFPS() : 0.0)
                << std::endl;
        }
    }
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
    if (!media->videoDecoder)
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
            media->videoDecoder->GetContext();

        int fps =
            static_cast<int>(
                media->videoFrameDuration > 0 ?
                1.0 / media->videoFrameDuration + 0.5 :
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
        media->demuxer &&
        media->demuxer->GetAudioStream())
    {
        AVCodecParameters* ap =
            media->demuxer->GetAudioStream()->codecpar;

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

#include "Renderer.h"
#include "Utils/Logger.h"

#include "Player.h"
#include "Utils/Logger.h"

#include <SDL.h>
#include "Utils/Logger.h"

#include <iostream>
#include "Utils/Logger.h"
#include <string>
#include "Utils/Logger.h"
#include <sstream>
#include "Utils/Logger.h"
#include <iomanip>
#include "Utils/Logger.h"

// ============================================================
// 渲染一帧
//
// 流程：YUV -> RGB24 -> 纹理 -> 等比缩放绘制 -> OSD -> 标题
// 同步等待 / 事件处理都在 Player::Run 里，这里只负责画
// ============================================================

bool RenderFrame(
    AVFrame* frame,
    SDL_Window* window,
    SDL_Renderer* renderer,
    SDL_Texture* texture,
    Player* player,
    bool& quit)
{
    // ---------- YUV -> RGB 转换 ----------

    SwsContext* swsCtx =
        player->GetSwsContext();              // 颜色空间转换器

    uint8_t* rgbData =
        player->GetRGBData();                 // RGB 缓冲首地址

    int rgbLinesize =
        player->GetRGBLinesize();             // RGB 每行字节数

    SDL_Texture* rgbTexture =
        player->GetRGBTexture();              // RGB 纹理

    if (!swsCtx || !rgbData || !rgbTexture)
    {
        return false;
    }

    sws_scale(
        swsCtx,                               // 转换器

        frame->data,                          // 输入 YUV 数据

        frame->linesize,                      // 输入每行字节数

        0,                                    // 从第 0 行开始

        frame->height,                        // 转换整张图

        &rgbData,                             // 输出 RGB

        &rgbLinesize);                        // 输出每行字节数

    // ---------- 上传纹理 ----------

    SDL_UpdateTexture(
        rgbTexture,
        nullptr,                              // 更新整张纹理

        rgbData,                              // RGB 数据

        rgbLinesize);                         // 每行字节数

    // ---------- 等比缩放绘制（保持宽高比） ----------

    SDL_RenderClear(renderer);

    int windowWidth = 0;

    int windowHeight = 0;

    SDL_GetWindowSize(
        window,
        &windowWidth,
        &windowHeight);

    SDL_Rect dstRect;

    double videoRatio =
        static_cast<double>(
            player->GetVideoWidth())
        /
        player->GetVideoHeight();             // 视频宽高比

    double windowRatio =
        static_cast<double>(
            windowWidth)
        /
        windowHeight;                         // 窗口宽高比

    if (windowRatio > videoRatio)
    {
        // 窗口更宽：按高度撑满，宽度等比
        dstRect.h = windowHeight;

        dstRect.w =
            static_cast<int>(
                windowHeight *
                videoRatio);

        dstRect.x =
            (windowWidth - dstRect.w) / 2;

        dstRect.y = 0;
    }
    else
    {
        // 窗口更高：按宽度撑满，高度等比
        dstRect.w = windowWidth;

        dstRect.h =
            static_cast<int>(
                windowWidth /
                videoRatio);

        dstRect.x = 0;

        dstRect.y =
            (windowHeight - dstRect.h) / 2;
    }

    SDL_RenderCopy(
        renderer,
        rgbTexture,
        nullptr,                              // 整张纹理

        &dstRect);                            // 绘制到目标矩形

    // ---------- OSD ----------

    RenderOSD(
        renderer,
        player);

    SDL_RenderPresent(renderer);

    // ---------- 窗口标题 ----------

    UpdateWindowTitle(
        window,
        player);

    return true;
}

// ============================================================
// 初始化 SDL（视频 + 音频）
// ============================================================

bool InitSDL(
    int width,
    int height,
    SDL_Window*& window,
    SDL_Renderer*& renderer,
    SDL_Texture*& texture)
{
    // 视频渲染 + 音频输出都需要
    if (SDL_Init(
        SDL_INIT_VIDEO |
        SDL_INIT_AUDIO) < 0)
    {
        Logger::Info()
            << "[SDL] Init failed : "
            << SDL_GetError()
            << std::endl;

        return false;
    }

    window =
        SDL_CreateWindow(
            "FFmpeg Player",
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            width,
            height,
            SDL_WINDOW_SHOWN |
            SDL_WINDOW_RESIZABLE);

    if (!window)
    {
        Logger::Info()
            << "[SDL] CreateWindow failed : "
            << SDL_GetError()
            << std::endl;

        SDL_Quit();

        return false;
    }

    renderer =
        SDL_CreateRenderer(
            window,
            -1,
            SDL_RENDERER_ACCELERATED);

    if (!renderer)
    {
        Logger::Info()
            << "[SDL] CreateRenderer failed : "
            << SDL_GetError()
            << std::endl;

        SDL_DestroyWindow(window);

        SDL_Quit();

        return false;
    }

    texture =
        SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_IYUV,
            SDL_TEXTUREACCESS_STREAMING,
            width,
            height);

    if (!texture)
    {
        Logger::Info()
            << "[SDL] CreateTexture failed : "
            << SDL_GetError()
            << std::endl;

        SDL_DestroyRenderer(renderer);

        SDL_DestroyWindow(window);

        SDL_Quit();

        return false;
    }

    return true;
}

// ============================================================
// 更新窗口标题
// ============================================================

void UpdateWindowTitle(
    SDL_Window* window,
    Player* player)
{
    if (!window ||
        !player)
    {
        return;
    }

    std::string state;

    switch (player->GetState())
    {
    case PlayerState::Playing:
        state = "Playing";
        break;

    case PlayerState::Paused:
        state = "Paused";
        break;

    case PlayerState::EndOfFile:
        state = "EOF";
        break;

    default:
        state = "Stopped";
        break;
    }

    std::ostringstream speedStream;

    speedStream
        << std::fixed
        << std::setprecision(1)
        << player->GetPlaybackSpeed();

    std::ostringstream oss;

    oss
        << std::fixed
        << std::setprecision(2)
        << player->GetProgress() * 100.0;

    std::string title =
        "FFmpeg Player | " +
        state +
        " | " +
        player->FullScreenToString() +
        " | " +
        speedStream.str() +
        "x | " +
        "Volume " +
        std::to_string(player->GetVolume()) +
        " | " +
        player->GetTimeString() +
        " / " +
        player->GetDurationString() +
        " | " +
        oss.str() +
        "%";

    SDL_SetWindowTitle(
        window,
        title.c_str());
}

// ============================================================
// 绘制 OSD
// ============================================================

void RenderOSD(
    SDL_Renderer* renderer,
    Player* player)
{
    if (!renderer ||
        !player)
    {
        return;
    }

    OSDManager* osd =
        player->GetOSDManager();

    if (!osd)
    {
        return;
    }

    osd->Update(
        renderer,
        player);

    osd->Render(
        renderer);
}

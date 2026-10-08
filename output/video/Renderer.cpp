#include "output/video/Renderer.h"
#include "infra/Logger.h"

#include "output/video/ControlBarState.h"
#include "output/osd/OSDManager.h"
#include "infra/Logger.h"

#include <SDL.h>
#include "infra/Logger.h"

#include <iostream>
#include "infra/Logger.h"
#include <string>
#include "infra/Logger.h"
#include <sstream>
#include "infra/Logger.h"
#include <iomanip>
#include "infra/Logger.h"

// ============================================================
// 渲染一帧
//
// 流程：YUV -> RGB24 -> 纹理 -> 等比缩放绘制 -> OSD -> 标题
// 同步等待 / 事件处理都在 Player::Run 里，这里只负责画
// ============================================================

bool RenderFrame(
    AVFrame* frame,
    const RenderContext& ctx,
    bool& quit)
{
    SDL_Window* window =
        ctx.window;

    SDL_Renderer* renderer =
        ctx.renderer;
    // ---------- YUV -> RGB 转换 ----------

    SwsContext* swsCtx =
        ctx.sws;              // 颜色空间转换器

    uint8_t* rgbData =
        ctx.rgbData;                 // RGB 缓冲首地址

    int rgbLinesize =
        ctx.rgbLinesize;             // RGB 每行字节数

    SDL_Texture* rgbTexture =
        ctx.rgbTexture;              // RGB 纹理

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
            ctx.videoWidth)
        /
        ctx.videoHeight;             // 视频宽高比

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
        ctx);

    // ---------- Control bar (8.14) ----------

    RenderControlBar(
        ctx);

    SDL_RenderPresent(renderer);

    // ---------- 窗口标题 ----------

    UpdateWindowTitle(
        ctx);

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
    const RenderContext& ctx)
{
    if (!ctx.window)
    {
        return;
    }

    // phase 8.1: state label comes from core (same mapping as before)
    std::string state =
        ctx.stateText ? ctx.stateText : "Stopped";

    std::ostringstream speedStream;

    speedStream
        << std::fixed
        << std::setprecision(1)
        << ctx.speed;

    std::ostringstream oss;

    oss
        << std::fixed
        << std::setprecision(2)
        << ctx.progress * 100.0;

    std::string title =
        "FFmpeg Player | " +
        state +
        " | " +
        ctx.fullScreenText +
        " | " +
        speedStream.str() +
        "x | " +
        "Volume " +
        std::to_string(ctx.volume) +
        " | " +
        ctx.timeString +
        " / " +
        ctx.durationString +
        " | " +
        oss.str() +
        "%";

    SDL_SetWindowTitle(
        ctx.window,
        title.c_str());
}

// ============================================================
// 绘制 OSD
// ============================================================

void RenderOSD(
    const RenderContext& ctx)
{
    if (!ctx.renderer)
    {
        return;
    }

    OSDManager* osd =
        ctx.osd;

    if (!osd)
    {
        return;
    }

    osd->Update(
        ctx.renderer,
        ctx.stats);

    osd->Render(
        ctx.renderer);
}

// ============================================================
// Control bar (8.14): prev / play-pause / next + seek bar
// ============================================================

static void FillTriangle(
    SDL_Renderer* renderer,
    int x1,
    int y1,
    int x2,
    int y2,
    int x3,
    int y3)
{
    SDL_Vertex verts[3];

    SDL_Color col = { 255, 255, 255, 255 };

    verts[0].position = { (float)x1, (float)y1 };
    verts[0].color = col;
    verts[0].tex_coord = { 0.0f, 0.0f };

    verts[1].position = { (float)x2, (float)y2 };
    verts[1].color = col;
    verts[1].tex_coord = { 0.0f, 0.0f };

    verts[2].position = { (float)x3, (float)y3 };
    verts[2].color = col;
    verts[2].tex_coord = { 0.0f, 0.0f };

    SDL_RenderGeometry(
        renderer,
        nullptr,
        verts,
        3,
        nullptr,
        0);
}

void RenderControlBar(
    const RenderContext& ctx)
{
    if (!ctx.renderer ||
        !ctx.bar)
    {
        return;
    }

    SDL_Renderer* renderer =
        ctx.renderer;

    SDL_Window* window =
        ctx.window;

    if (!window)
    {
        return;
    }

    int winW = 0;

    int winH = 0;

    SDL_GetWindowSize(
        window,
        &winW,
        &winH);

    if (winW <= 0 || winH <= 0)
    {
        return;
    }

    ControlBarState& ui =
        *ctx.bar;

    const int barH = 46;             // bar height

    const int btnSize = 34;          // button size

    const int btnY =
        winH - barH + 6;

    const int btnGap = 8;

    const int leftX = 14;

    // ---------- layout (single source of truth) ----------

    ui.prevBtn = {
        leftX,
        btnY,
        btnSize,
        btnSize
    };

    ui.playBtn = {
        leftX + btnSize + btnGap,
        btnY,
        btnSize,
        btnSize
    };

    ui.nextBtn = {
        leftX + 2 * (btnSize + btnGap),
        btnY,
        btnSize,
        btnSize
    };

    const int trackX =
        leftX + 3 * (btnSize + btnGap) -
        btnGap + 10;

    const int trackW =
        winW - trackX - 14;

    const int trackH = 6;

    const int trackY = winH - 24;

    ui.track = {
        trackX,
        trackY - 8,                 // taller hit area
        trackW,
        trackH + 16
    };

    // ---------- background ----------

    SDL_SetRenderDrawBlendMode(
        renderer,
        SDL_BLENDMODE_BLEND);

    SDL_Rect bg = {
        0,
        winH - barH,
        winW,
        barH
    };

    SDL_SetRenderDrawColor(
        renderer,
        0, 0, 0, 150);

    SDL_RenderFillRect(
        renderer,
        &bg);

    // ---------- progress ----------

    double duration =
        ctx.duration;

    double progress =
        ctx.progress;

    if (duration > 0.0)
    {
        if (ui.seekDragging &&
            ui.seekPreview >= 0.0)
        {
            progress =
                ui.seekPreview / duration;
        }

        if (progress < 0.0)
        {
            progress = 0.0;
        }

        if (progress > 1.0)
        {
            progress = 1.0;
        }

        SDL_Rect trackBg = {
            trackX,
            trackY,
            trackW,
            trackH
        };

        SDL_SetRenderDrawColor(
            renderer,
            60, 60, 60, 220);

        SDL_RenderFillRect(
            renderer,
            &trackBg);

        int playedW =
            (int)(trackW * progress);

        if (playedW > 0)
        {
            SDL_Rect played = {
                trackX,
                trackY,
                playedW,
                trackH
            };

            SDL_SetRenderDrawColor(
                renderer,
                80, 160, 255, 255);

            SDL_RenderFillRect(
                renderer,
                &played);
        }

        // knob
        int knobX =
            trackX + playedW - 4;

        SDL_Rect knob = {
            knobX,
            trackY - 3,
            8,
            trackH + 6
        };

        SDL_SetRenderDrawColor(
            renderer,
            255, 255, 255, 255);

        SDL_RenderFillRect(
            renderer,
            &knob);
    }

    // ---------- buttons ----------

    bool paused =
        ctx.paused;

    // hover highlight
    if (ui.hoverButton == 1)
    {
        SDL_SetRenderDrawColor(
            renderer,
            255, 255, 255, 40);

        SDL_RenderFillRect(
            renderer,
            &ui.prevBtn);
    }
    else if (ui.hoverButton == 2)
    {
        SDL_SetRenderDrawColor(
            renderer,
            255, 255, 255, 40);

        SDL_RenderFillRect(
            renderer,
            &ui.playBtn);
    }
    else if (ui.hoverButton == 3)
    {
        SDL_SetRenderDrawColor(
            renderer,
            255, 255, 255, 40);

        SDL_RenderFillRect(
            renderer,
            &ui.nextBtn);
    }

    int cx = 0;

    int cy = 0;

    // prev icon: two left triangles
    cx = ui.prevBtn.x + ui.prevBtn.w / 2;

    cy = ui.prevBtn.y + ui.prevBtn.h / 2;

    FillTriangle(
        renderer,
        cx - 8, cy - 8,
        cx - 8, cy + 8,
        cx + 2, cy);

    FillTriangle(
        renderer,
        cx - 2, cy - 8,
        cx - 2, cy + 8,
        cx + 8, cy);

    // next icon: two right triangles
    cx = ui.nextBtn.x + ui.nextBtn.w / 2;

    cy = ui.nextBtn.y + ui.nextBtn.h / 2;

    FillTriangle(
        renderer,
        cx + 8, cy - 8,
        cx + 8, cy + 8,
        cx - 2, cy);

    FillTriangle(
        renderer,
        cx + 2, cy - 8,
        cx + 2, cy + 8,
        cx - 8, cy);

    // play / pause icon
    cx = ui.playBtn.x + ui.playBtn.w / 2;

    cy = ui.playBtn.y + ui.playBtn.h / 2;

    if (paused)
    {
        // play: right triangle
        FillTriangle(
            renderer,
            cx - 4, cy - 9,
            cx - 4, cy + 9,
            cx + 10, cy);
    }
    else
    {
        // pause: two bars
        SDL_Rect bar1 = {
            cx - 8,
            cy - 9,
            5,
            18
        };

        SDL_Rect bar2 = {
            cx + 3,
            cy - 9,
            5,
            18
        };

        SDL_RenderFillRect(
            renderer,
            &bar1);

        SDL_RenderFillRect(
            renderer,
            &bar2);
    }
}

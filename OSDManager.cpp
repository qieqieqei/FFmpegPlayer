#include "OSDManager.h"

#include "FontManager.h"

#include "Player.h"

#include "Statistics/PlayerStatistics.h"

#include <sstream>
#include <iomanip>
#include <iostream>

// 纹理重建最小间隔（毫秒）
static const int kRebuildIntervalMs = 200;

OSDManager::OSDManager()
{
}

OSDManager::~OSDManager()
{
    Close();
}

bool OSDManager::Init(
    FontManager* manager)
{
    if (!manager)
    {
        return false;
    }

    fontManager = manager;

    // OSD 区域（左上角）
    rect.x = 20;

    rect.y = 20;

    rect.w = 600;

    rect.h = 240;

    visible = false;

    alpha = 255;

    lastBuildTime =
        std::chrono::steady_clock::now();

    return true;
}

void OSDManager::Update(
    SDL_Renderer* renderer,
    Player* player)
{
    if (!renderer ||
        !player ||
        !fontManager)
    {
        return;
    }

    // ---------- 拼装文本 ----------

    PlayerStatistics* stats =
        player->GetStatistics();

    std::ostringstream oss;

    oss
        << "FFmpeg Player\n"
        << "State : "
        << player->StateToString()
        << "\n"
        << "Time : "
        << player->GetTimeString()
        << " / "
        << player->GetDurationString();

    if (stats)
    {
        oss
            << "\n"
            << "Resolution : "
            << stats->GetResolution()
            << "\n"
            << "Codec : "
            << stats->GetVideoCodec();

        if (!stats->GetAudioCodec().empty())
        {
            oss
                << " / "
                << stats->GetAudioCodec();
        }

        oss
            << "\n"
            << "FPS : "
            << std::fixed
            << std::setprecision(1)
            << stats->GetNominalFPS()
            << " (Measured "
            << stats->GetFPS()
            << ")"
            << "\n"
            << "Bitrate : "
            << PlayerStatistics::FormatBitrate(
                stats->GetBitrate())
            << "\n"
            << "Buffer : Video "
            << stats->GetVideoFrames()
            << " frames / Audio "
            << stats->GetAudioBufferMs()
            << " ms"
            << "\n"
            << "Dropped : "
            << stats->GetDroppedFrames();
    }

    oss
        << "\n"
        << "Speed : "
        << std::fixed
        << std::setprecision(1)
        << player->GetPlaybackSpeed()
        << "x"
        << "\n"
        << "Volume : "
        << player->GetVolume()
        << "%";

    std::string text =
        oss.str();

    // ---------- 节流：200ms 内不重复重建 ----------

    auto now =
        std::chrono::steady_clock::now();

    auto elapsed =
        std::chrono::duration_cast<
            std::chrono::milliseconds>(
            now - lastBuildTime)
            .count();

    if (text == lastText &&
        elapsed < kRebuildIntervalMs)
    {
        // 文本没变且没到时间：不重建
        return;
    }

    if (elapsed < kRebuildIntervalMs)
    {
        // 文本变了但还没到时间：先不重建
        //（等下一帧再重建，避免每帧创建纹理）
        return;
    }

    lastBuildTime = now;

    lastText = text;

    if (texture)
    {
        SDL_DestroyTexture(texture);

        texture = nullptr;
    }

    SDL_Color color;

    color.r = 255;

    color.g = 255;

    color.b = 255;

    color.a = 255;

    texture =
        fontManager->CreateMultilineTextTexture(
            renderer,
            text,
            color);

    if (!texture)
    {
        return;
    }

    Show();
}

void OSDManager::Render(
    SDL_Renderer* renderer)
{
    if (!renderer ||
        !texture ||
        !visible)
    {
        return;
    }

    // 半透明背景
    SDL_SetRenderDrawBlendMode(
        renderer,
        SDL_BLENDMODE_BLEND);

    SDL_SetRenderDrawColor(
        renderer,
        0,
        0,
        0,
        120);

    SDL_Rect bg =
        rect;

    bg.x -= 10;

    bg.y -= 10;

    bg.w += 20;

    bg.h += 20;

    SDL_RenderFillRect(
        renderer,
        &bg);

    // 文字
    SDL_SetTextureAlphaMod(
        texture,
        255);

    SDL_SetTextureBlendMode(
        texture,
        SDL_BLENDMODE_BLEND);

    SDL_RenderCopy(
        renderer,
        texture,
        nullptr,
        &rect);

    // ---------- 字幕（6.9）：底部居中 ----------

    // 文本变化时标记重建（渲染线程调用，直接重建）
    if (subtitleDirty)
    {
        RebuildSubtitle(renderer);
    }

    if (subtitleTexture)
    {
        int winW = 0;

        int winH = 0;

        SDL_GetRendererOutputSize(
            renderer,
            &winW,
            &winH);

        int tw = 0;

        int th = 0;

        SDL_QueryTexture(
            subtitleTexture,
            nullptr,
            nullptr,
            &tw,
            &th);

        // 居中，距离底部 40px
        SDL_Rect subRect;

        subRect.x =
            (winW - tw) / 2;

        subRect.y =
            winH - th - 40;

        subRect.w = tw;

        subRect.h = th;

        // 半透明背景（增强可读性）
        SDL_SetRenderDrawBlendMode(
            renderer,
            SDL_BLENDMODE_BLEND);

        SDL_SetRenderDrawColor(
            renderer,
            0,
            0,
            0,
            160);

        SDL_Rect subBg;

        subBg.x = subRect.x - 10;

        subBg.y = subRect.y - 6;

        subBg.w = subRect.w + 20;

        subBg.h = subRect.h + 12;

        SDL_RenderFillRect(
            renderer,
            &subBg);

        // 字幕文字
        SDL_SetTextureAlphaMod(
            subtitleTexture,
            255);

        SDL_SetTextureBlendMode(
            subtitleTexture,
            SDL_BLENDMODE_BLEND);

        SDL_RenderCopy(
            renderer,
            subtitleTexture,
            nullptr,
            &subRect);
    }
}

void OSDManager::SetSubtitle(
    SDL_Renderer* renderer,
    const std::string& text)
{
    if (text == subtitleText)
    {
        return;
    }

    subtitleText = text;

    // 渲染线程在 Render 时重建纹理
    subtitleDirty = true;
}

void OSDManager::RebuildSubtitle(
    SDL_Renderer* renderer)
{
    subtitleDirty = false;

    // 销毁旧纹理
    if (subtitleTexture)
    {
        SDL_DestroyTexture(subtitleTexture);

        subtitleTexture = nullptr;
    }

    // 空文本：不显示
    if (subtitleText.empty() ||
        !fontManager)
    {
        return;
    }

    SDL_Color color;

    color.r = 255;

    color.g = 255;

    color.b = 255;

    color.a = 255;

    // 创建字幕纹理
    subtitleTexture =
        fontManager->CreateMultilineTextTexture(
            renderer,
            subtitleText,
            color);
}

void OSDManager::Show()
{
    visible = true;

    alpha = 255;
}

void OSDManager::Close()
{
    if (texture)
    {
        SDL_DestroyTexture(
            texture);

        texture = nullptr;
    }

    if (subtitleTexture)
    {
        SDL_DestroyTexture(
            subtitleTexture);

        subtitleTexture = nullptr;
    }

    subtitleText.clear();

    subtitleDirty = false;

    visible = false;

    alpha = 0;
}

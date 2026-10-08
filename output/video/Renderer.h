#pragma once

#include <SDL.h>

#include "output/video/RenderContext.h"

extern "C"
{
#include <libavutil/frame.h>
}

// 渲染一帧（YUV -> RGB -> 纹理 -> 绘制 + OSD + 窗口标题）
// 注意：不做事件处理、不做同步等待（都移到 Player::Run）
bool RenderFrame(
    AVFrame* frame,
    const RenderContext& ctx,
    bool& quit);

// 初始化 SDL（视频 + 音频）
bool InitSDL(
    int width,
    int height,
    SDL_Window*& window,
    SDL_Renderer*& renderer,
    SDL_Texture*& texture);

// 更新窗口标题（状态 / 时间 / 进度 / 倍速 / 音量）
void UpdateWindowTitle(
    const RenderContext& ctx);

// 绘制 OSD 文字
void RenderOSD(
    const RenderContext& ctx);

// 绘制底部控制栏（上一个 / 暂停 / 下一个 + 可拖动进度条，8.14）
void RenderControlBar(
    const RenderContext& ctx);

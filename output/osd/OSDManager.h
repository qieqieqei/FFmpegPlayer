#pragma once

#include <SDL.h>
#include <chrono>
#include <string>

class Player;
class FontManager;

// ============================================================
// OSDManager - 屏幕文字叠加（5.7）
//
// 显示内容：
//   - 状态 / 时间 / 进度
//   - 分辨率 / 编码 / FPS / 码率
//   - 缓冲 / 丢帧
//   - 倍速 / 音量
//
// 优化：文字纹理每 200ms 最多重建一次
//（时间每秒变几十次，全速重建浪费 CPU）
// ============================================================

class OSDManager
{
public:

    OSDManager();

    ~OSDManager();

    bool Init(
        FontManager* manager);

    void Update(
        SDL_Renderer* renderer,
        Player* player);

    void Render(
        SDL_Renderer* renderer);

    void Close();

    void Show();// 触发OSD显示

    // 字幕（6.9）：底部居中显示当前字幕文本
    void SetSubtitle(
        SDL_Renderer* renderer,
        const std::string& text);

private:

    FontManager* fontManager = nullptr;    // 字体管理器

    SDL_Texture* texture = nullptr;        // 缓存文字纹理

    SDL_Rect rect;                         // OSD 显示区域

    std::string lastText;                  // 上一次文本（避免重复重建）

    bool visible = false;

    Uint8 alpha = 255;

    // 纹理重建节流：上次重建时刻
    std::chrono::steady_clock::time_point lastBuildTime;

    // 字幕（6.9）
    SDL_Texture* subtitleTexture = nullptr;   // 字幕纹理

    std::string subtitleText;                 // 当前字幕文本

    bool subtitleDirty = false;               // 文本变化待重建标记

    void RebuildSubtitle(
        SDL_Renderer* renderer);              // 重建字幕纹理

};

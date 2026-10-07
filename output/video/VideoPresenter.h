#pragma once

#include <memory>

#include <SDL.h>

extern "C"
{
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

#include "infra/FFmpegPtr.h"

// ============================================================
// VideoPresenter - SDL presentation resources (phase 5.2)
//
// Owns the per-media presentation state that used to live in Player:
//   - SDL window / renderer / textures (created by InitSDL in Renderer.cpp)
//   - YUV -> RGB swscale context, recreated when the frame format changes
//   - RGB24 staging buffer used by RenderFrame
//   - the last rendered frame (screenshot / EOF display)
//
// Lifetime: one instance per loaded media. Created by PlaybackSession
// (OpenMedia) and destroyed by PlaybackSession (ReleaseMedia) - the same
// points that used to create/destroy the SDL resources before.
//
// Dependency note: lives in output/, so it must not include core/.
// It only takes plain data (width / height / frames) and returns raw
// handles; policy (which frame to show, when to go fullscreen) stays in
// Player / PlaybackSession.
// ============================================================

class VideoPresenter
{
public:

    // Creates the SDL window / renderer / textures and the RGB staging
    // buffer for a media of the given pixel size. Returns false when the
    // SDL resources cannot be created (same failure points as before).
    bool Create(
        int width,
        int height);

    // Releases everything created by Create(); safe to call twice.
    void Destroy();

    // Applies the window fullscreen flag (no-op before Create()).
    void ApplyFullscreen(
        bool on);

    SDL_Window* GetWindow() const;

    SDL_Renderer* GetRenderer() const;

    SDL_Texture* GetTexture() const;

    SDL_Texture* GetRGBTexture() const;

    // Returns the (lazily created) YUV -> RGB converter matching this
    // frame's format and size; rebuilt when the source format changes
    // (software YUV420P <-> hardware NV12).
    SwsContext* GetSwsForFrame(
        AVFrame* frame);

    uint8_t* GetRGBData() const;

    int GetRGBLinesize() const;

    // Keeps a private clone of the frame for screenshot / EOF display.
    void SetLastFrame(
        AVFrame* frame);

    AVFrame* GetLastFrame() const;

private:

    SDL_Window* window = nullptr;

    SDL_Renderer* renderer = nullptr;

    // YUV video texture (reserved; RenderFrame draws through rgbTexture)
    SDL_Texture* texture = nullptr;

    // RGB24 texture uploaded once per rendered frame
    SDL_Texture* rgbTexture = nullptr;

    // YUV -> RGB converter
    SwsContextPtr swsCtx;

    // Source format / size the converter was created for
    AVPixelFormat swsSrcFmt = AV_PIX_FMT_NONE;

    int swsSrcW = 0;

    int swsSrcH = 0;

    // RGB staging buffer (rgbLinesize bytes per row)
    std::unique_ptr<uint8_t[]> rgbData;

    int rgbLinesize = 0;

    // Clone of the last rendered frame
    AVFramePtr lastFrame;
};

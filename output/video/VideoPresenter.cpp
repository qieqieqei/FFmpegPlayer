#include "output/video/VideoPresenter.h"

#include "output/video/Renderer.h"
#include "infra/ErrorHandler.h"

// ============================================================
// Create / Destroy
// ============================================================

bool VideoPresenter::Create(
    int width,
    int height)
{
    // RGB24 staging buffer (3 bytes per pixel, packed rows)
    rgbLinesize =
        width * 3;

    rgbData =
        std::make_unique<uint8_t[]>(
            rgbLinesize * height);

    if (!InitSDL(
        width,
        height,
        window,
        renderer,
        texture))
    {
        return false;
    }

    rgbTexture =
        SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_RGB24,
            SDL_TEXTUREACCESS_STREAMING,
            width,
            height);

    if (!rgbTexture)
    {
        ErrorHandler::LogSDL(
            ErrorTag::Player,
            "SDL_CreateTexture (RGB)");

        return false;
    }

    return true;
}

void VideoPresenter::Destroy()
{
    lastFrame.reset();

    if (rgbTexture)
    {
        SDL_DestroyTexture(rgbTexture);

        rgbTexture = nullptr;
    }

    if (texture)
    {
        SDL_DestroyTexture(texture);

        texture = nullptr;
    }

    if (renderer)
    {
        SDL_DestroyRenderer(renderer);

        renderer = nullptr;
    }

    if (window)
    {
        SDL_DestroyWindow(window);

        window = nullptr;
    }

    rgbData.reset();

    rgbLinesize = 0;

    swsCtx.reset();

    swsSrcFmt = AV_PIX_FMT_NONE;

    swsSrcW = 0;

    swsSrcH = 0;
}

void VideoPresenter::ApplyFullscreen(
    bool on)
{
    if (!window)
    {
        return;
    }

    SDL_SetWindowFullscreen(
        window,
        on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

// ============================================================
// Accessors
// ============================================================

SDL_Window* VideoPresenter::GetWindow() const
{
    return window;
}

SDL_Renderer* VideoPresenter::GetRenderer() const
{
    return renderer;
}

SDL_Texture* VideoPresenter::GetTexture() const
{
    return texture;
}

SDL_Texture* VideoPresenter::GetRGBTexture() const
{
    return rgbTexture;
}

uint8_t* VideoPresenter::GetRGBData() const
{
    return rgbData.get();
}

int VideoPresenter::GetRGBLinesize() const
{
    return rgbLinesize;
}

AVFrame* VideoPresenter::GetLastFrame() const
{
    return lastFrame.get();
}

void VideoPresenter::SetLastFrame(
    AVFrame* frame)
{
    lastFrame.reset(
        frame ? av_frame_clone(frame) : nullptr);
}

// ============================================================
// YUV -> RGB converter (lazily created / rebuilt on format change)
// ============================================================

SwsContext* VideoPresenter::GetSwsForFrame(
    AVFrame* frame)
{
    if (!frame)
    {
        return nullptr;
    }

    AVPixelFormat fmt =
        static_cast<AVPixelFormat>(
            frame->format);

    // Same format / size: reuse the existing converter
    if (swsCtx &&
        swsSrcFmt == fmt &&
        swsSrcW == frame->width &&
        swsSrcH == frame->height)
    {
        return swsCtx.get();
    }

    // Changed (software YUV420P <-> hardware NV12, or a new media): rebuild
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

    return swsCtx.get();
}

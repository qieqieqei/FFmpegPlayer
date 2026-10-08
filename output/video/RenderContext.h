#pragma once

#include <SDL.h>
#include <string>

#include "output/osd/StatsSnapshot.h"

extern "C"
{
#include <libswscale/swscale.h>
}

class OSDManager;
struct ControlBarState;

// ============================================================
// RenderContext - render-time snapshot handed to Renderer
//
// Phase 8.1: replaces the Player* parameter of RenderFrame /
// UpdateWindowTitle / RenderOSD / RenderControlBar so that
// output/video no longer depends on core/Player.h. Pure data -
// no logic. Filled by core (Player::MakeRenderContext).
// ============================================================

struct RenderContext
{
    // SDL / presenter resources
    SDL_Window* window = nullptr;

    SDL_Renderer* renderer = nullptr;

    SDL_Texture* texture = nullptr;

    // YUV -> RGB conversion
    SwsContext* sws = nullptr;

    uint8_t* rgbData = nullptr;

    int rgbLinesize = 0;

    SDL_Texture* rgbTexture = nullptr;

    // video size
    int videoWidth = 0;

    int videoHeight = 0;

    // state snapshot (see Player::MakeRenderContext)
    const char* stateText = "Stopped";   // title label: Playing/Paused/EOF/Stopped

    bool paused = false;                 // control bar play/pause icon

    bool fullscreen = false;

    const char* fullScreenText = "Window";

    double speed = 1.0;

    double progress = 0.0;

    double duration = 0.0;

    int volume = 100;

    std::string timeString;

    std::string durationString;

    // output-side references (data hosts)
    OSDManager* osd = nullptr;

    ControlBarState* bar = nullptr;

    // OSD statistics snapshot
    StatsSnapshot stats;
};

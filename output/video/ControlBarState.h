#pragma once

#include <SDL.h>

// ============================================================
// ControlBarState - bottom control bar UI state (8.14)
//
// Phase 8.1: moved out of core/Player.h so that output/video can
// use it without depending on core (see docs/architecture/
// dependency.md). Data only - no logic.
// ============================================================

struct ControlBarState
{
    bool visible = true;          // always shown

    bool seekDragging = false;    // progress bar dragging

    double seekPreview = -1.0;    // drag preview time (s)

    int hoverButton = 0;          // 0=none 1=prev 2=play/pause 3=next

    SDL_Rect prevBtn{};           // prev button hit rect

    SDL_Rect playBtn{};           // play/pause button hit rect

    SDL_Rect nextBtn{};           // next button hit rect

    SDL_Rect track{};             // progress track hit rect
};

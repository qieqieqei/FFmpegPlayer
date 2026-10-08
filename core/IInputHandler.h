#pragma once

// ============================================================
// IInputHandler - input handling interface (phase 8.3)
//
// core/ (L5) must not depend on app/ (L6). Input handling lives in
// app/, so core defines this abstraction and app implements it.
// Player keeps a non-owning pointer and invokes HandleEvents once
// per iteration of the playback loop(s).
// ============================================================

struct IInputHandler
{
    virtual ~IInputHandler() = default;

    // Poll pending SDL events. Set quit = true to stop playback.
    virtual void HandleEvents(
        bool& quit) = 0;
};

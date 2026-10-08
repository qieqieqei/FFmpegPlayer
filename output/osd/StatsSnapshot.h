#pragma once

#include <string>

// ============================================================
// StatsSnapshot - read-only statistics snapshot for OSDManager
//
// Phase 8.2: OSDManager used to take Player* (core) and read
// PlayerStatistics (features) / NetworkStatistics (streaming).
// The dependency matrix forbids output -> features/streaming, so
// this struct holds plain values only. Filled by core
// (Player::MakeRenderContext).
// ============================================================

struct StatsSnapshot
{
    // switches (equivalent to the former if (stats) / if (net))
    bool hasStats = false;

    bool hasNetwork = false;

    // PlayerStatistics snapshot
    std::string resolution;

    std::string videoCodec;

    std::string audioCodec;

    std::string bitrateText;        // pre-formatted: FormatBitrate(bps)

    double nominalFps = 0.0;

    double decodeFps = 0.0;

    double renderFps = 0.0;

    int videoFrames = 0;

    int videoBufferMs = 0;

    int audioBufferMs = 0;

    int videoPackets = 0;

    int audioPackets = 0;

    int droppedFrames = 0;

    // NetworkStatistics snapshot
    std::string networkText;        // NetworkStatistics::ToString()

    // general state (same values as before)
    const char* stateText = "";

    std::string timeString;

    std::string durationString;

    bool hardwareDecode = false;

    double speed = 1.0;

    int volume = 100;
};

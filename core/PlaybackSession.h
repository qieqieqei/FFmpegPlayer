#pragma once

// ============================================================
// PlaybackSession - playback session (extracted in phase 4.4)
//
// Responsibilities:
//   - open / release / switch media (OpenMedia / ReleaseMedia / SwitchMedia)
//   - thread start/stop (StartThreads / StopThreads) and the three decode
//     loops (DemuxLoop / VideoDecodeLoop / AudioDecodeLoop)
//   - inter-thread flags plus seek / reconnect / switch state
//
// Dependencies (injected):
//   - Player& owner       : host for rendering, statistics, output chain
//   - MediaContext& media : media pipeline objects (owned by Player,
//                           rebuilt on every media load)
// ============================================================

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "core/MediaContext.h"

class Player;

class PlaybackSession
{
    friend class Player;

public:

    PlaybackSession(
        Player& owner,
        MediaContext& media);

    ~PlaybackSession();

    bool OpenMedia(
        const std::string& path);

    bool SwitchMedia(
        const std::string& path);

    void ReleaseMedia();

    bool StartThreads();

    void StopThreads();

private:

    void DemuxLoop();

    void VideoDecodeLoop();

    void AudioDecodeLoop();

    void FlushVideoDecoder();

    bool SendVideoPacket(
        AVPacket* pkt);

    DecodeResult ReceiveVideoFrame(
        FramePtr& out);

    void TryInitHardwareDecoder(
        AVCodecParameters* codecpar);

    void ProcessAudioFrame(
        AVFrame* frame);

    void AudioSeekCleanup(
        double target);

    Player& owner;

    MediaContext& media;

    std::thread demuxThread;

    std::thread videoThread;

    std::thread audioThread;

    std::atomic<bool> quit{ false };

    std::atomic<bool> demuxEof{ false };

    std::atomic<bool> videoEof{ false };

    std::atomic<bool> audioEof{ false };

    std::atomic<bool> audioAbort{ false };

    double seekPosition = 0.0;

    bool seekPending = false;

    double dropAudioUntil = -1.0;

    bool autoAdvancing = false;

    std::string currentMediaPath;

    std::atomic<bool> reconnectRequested{ false };

    std::atomic<int> reconnectAttempts{ 0 };

    bool switchRequested = false;

    std::string switchPath;
};

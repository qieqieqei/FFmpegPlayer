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

    // assembly / teardown (phase 8.5a, moved from Player::Init / Player::Close;
    // Player keeps thin facade forwarders so app/main call sites are unchanged)
    bool Prepare(
        const char* filename);

    void Shutdown();

    // ---- packet queue routing (phase 5.1, moved from Player) ----

    bool PushVideoPacket(
        PacketPtr&& pkt);

    bool PushAudioPacket(
        PacketPtr&& pkt);

    PacketPtr PopVideoPacket(
        int timeoutMs);

    PacketPtr PopAudioPacket(
        int timeoutMs);

    bool IsVideoQueueInterrupted() const;

    bool IsAudioQueueInterrupted() const;

    int GetVideoQueueSize() const;

    int GetAudioQueueSize() const;

    int GetVideoQueueCapacity() const;

    // seek orchestration (phase 6.3, moved from Player; Player keeps
    // thin facade forwarders so app/Event call sites are unchanged)
    void RequestSeek(
        double seconds);

    bool HasSeekRequest() const;

    double GetSeekPosition() const;

    bool IsSeekHandled() const;

    void ClearSeekHandled();

    // playlist navigation (phase 6.4, moved from Player; Player keeps
    // thin facade forwarders so app/Event/main call sites are unchanged)
    void AddToPlaylist(
        const std::string& path);

    void ExpandPlaylistWithSiblings();

    bool PlayPrevious();

    bool PlayNext();

    size_t GetPlaylistIndex() const;

    size_t GetPlaylistCount() const;

    const std::string& GetCurrentPath() const;

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

    // Frame acquisition + A/V sync (phase 5.3b, moved from Player::Run).
    // FrameAction tells the render loop whether to present or continue.
    enum class FrameAction
    {
        Skip,
        Present
    };

    FrameAction AcquireAndSyncFrame(
        FramePtr& frame,
        double& pts,
        bool& quit,
        bool& lastBufferingBlock);

    // Present one decoded frame (phase 5.3): render + render stats +
    // screenshot snapshot + time/progress + buffer-gate transition.
    // Moved out of Player::Run so the loop keeps only the frame
    // acquisition and A/V sync state machine.
    void PresentFrame(
        AVFrame* frame,
        double pts,
        bool& quit,
        bool& lastBufferingBlock);

    // switch / reconnect orchestration (phase 5.4, moved from Player::Run)
    void HandleSwitchRequest(
        bool& quit);

    void HandleReconnect(
        bool& quit);

    // statistics refresh (phase 6.2, moved from Player; called by the
    // render loop each frame).
    void UpdateStatistics();

    // drop-count anchors for NetworkBuffer live stats (demux thread only)

    int64_t lastVideoDropped = 0;

    int64_t lastAudioDropped = 0;

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

#pragma once

// OutputPipeline (phase 7.1)
// Owns the shared output chain: video/audio encoders + recording FLV muxer
// + RTMP publisher + HLS muxer, and feeds decoded frames into them.
// Layer: recording (L4). Must NOT depend on core/* (no Player/PlaybackSession).

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
}

#include "infra/FFmpegPtr.h"

class VideoEncoder;
class AudioEncoder;
class FLVMuxer;
class RTMPPublisher;
class HLSMuxer;
class ConfigManager;

class OutputPipeline
{
public:

    // Source parameters needed to build the encoders; injected by the
    // session when a media is opened (keeps OutputPipeline free of core).
    struct SourceInfo
    {
        bool hasVideo = false;
        int width = 0;
        int height = 0;
        int fps = 25;
        bool hasAudio = false;
        int sampleRate = 48000;
        int channels = 2;
    };

    explicit OutputPipeline(
        ConfigManager* config);

    ~OutputPipeline();

    void SetSourceInfo(
        const SourceInfo& info);

    // ---- frame feed (decode threads; outMutex protected) ----
    void FeedOutputVideo(
        AVFrame* frame);

    void FeedOutputAudio(
        AVFrame* frame);

    // ---- recording ----
    bool StartRecording(
        const std::string& path);

    void StopRecording();

    void ToggleRecording();

    bool IsRecording() const;

    // ---- RTMP pushing ----
    bool StartPushing(
        const std::string& url);

    void StopPushing();

    void TogglePushing();

    bool IsPushing() const;

    // ---- HLS ----
    bool StartHLS(
        const std::string& dir);

    void StopHLS();

    void ToggleHLS();

    bool IsHLSActive() const;

    // ---- lifecycle ----
    void StopAllOutputs();

    void ReleaseOutEncoders();

private:

    bool EnsureOutEncoders();

    AVFrame* ToYuv420p(
        AVFrame* frame);

    void DispatchVideoPacket(
        AVPacket* pkt);

    void DispatchAudioPacket(
        AVPacket* pkt);

    void FlushOutEncoders();

    ConfigManager* config = nullptr;

    SourceInfo srcInfo;

    std::mutex outMutex;             // guards output chain lifetime

    std::unique_ptr<VideoEncoder> outVideoEncoder;   // shared video encoder

    std::unique_ptr<AudioEncoder> outAudioEncoder;   // shared audio encoder

    std::unique_ptr<FLVMuxer> recordMuxer;           // recording (.flv file)

    std::unique_ptr<RTMPPublisher> rtmpPublisher;    // RTMP push

    std::unique_ptr<HLSMuxer> hlsMuxer;              // HLS segments

    SwsContextPtr outSws;                            // frame -> YUV420P

    AVFramePtr outYuvFrame;                          // converted frame (reused)

    int64_t outVideoPts = 0;                   // output video pts

    int64_t outAudioPts = 0;                   // output audio pts

    int64_t outVideoPktIdx = 0;                // output video packet index

    int64_t outAudioPktIdx = 0;                // output audio packet index

    bool recording = false;                    // recording active

    bool pushing = false;                      // pushing active

    bool hlsActive = false;                    // HLS active
};

#include "Statistics/PlayerStatistics.h"

#include <SDL.h>

#include <sstream>
#include <iomanip>

extern "C" {
#include <libavcodec/avcodec.h>
}

PlayerStatistics::PlayerStatistics()
{
}

void PlayerStatistics::Init(
    AVFormatContext* fmt,
    int videoIndex,
    int audioIndex)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (fmt &&
        videoIndex >= 0 &&
        videoIndex < static_cast<int>(fmt->nb_streams))
    {
        AVStream* videoStream =
            fmt->streams[videoIndex];

        std::ostringstream res;

        res
            << videoStream->codecpar->width
            << "x"
            << videoStream->codecpar->height;

        resolution = res.str();

        videoCodec =
            avcodec_get_name(
                videoStream->codecpar->codec_id);

        // 标称 FPS
        AVRational fpsRat =
            av_guess_frame_rate(
                fmt,
                videoStream,
                nullptr);

        if (fpsRat.num > 0 &&
            fpsRat.den > 0)
        {
            nominalFps =
                static_cast<double>(fpsRat.num) /
                fpsRat.den;
        }
        else
        {
            nominalFps = 25.0;
        }
    }

    if (fmt &&
        audioIndex >= 0 &&
        audioIndex < static_cast<int>(fmt->nb_streams))
    {
        audioCodec =
            avcodec_get_name(
                fmt->streams[audioIndex]
                    ->codecpar->codec_id);
    }

    lastFpsTime =
        SDL_GetTicks() / 1000.0;
}

void PlayerStatistics::OnFrameRendered()
{
    std::lock_guard<std::mutex> lock(mutex);

    frameCount++;

    UpdateFPS();
}

void PlayerStatistics::OnFrameDropped()
{
    std::lock_guard<std::mutex> lock(mutex);

    droppedFrames++;
}

void PlayerStatistics::UpdateBuffers(
    int videoPackets,
    int audioPackets,
    int videoFrames,
    int audioBufferBytes,
    int audioSampleRate,
    int audioChannels)
{
    std::lock_guard<std::mutex> lock(mutex);

    this->videoPackets = videoPackets;

    this->audioPackets = audioPackets;

    this->videoFrames = videoFrames;

    // 音频缓冲毫秒数
    if (audioSampleRate > 0 &&
        audioChannels > 0)
    {
        int bytesPerSec =
            audioSampleRate *
            audioChannels *
            2;   // S16

        if (bytesPerSec > 0)
        {
            audioBufferMs =
                static_cast<int>(
                    static_cast<double>(audioBufferBytes) /
                    bytesPerSec *
                    1000.0);
        }
    }
}

void PlayerStatistics::SetBitrate(
    double bps)
{
    std::lock_guard<std::mutex> lock(mutex);

    bitrate = bps;
}

double PlayerStatistics::GetFPS() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return fps;
}

double PlayerStatistics::GetNominalFPS() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return nominalFps;
}

double PlayerStatistics::GetBitrate() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return bitrate;
}

int PlayerStatistics::GetVideoPackets() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return videoPackets;
}

int PlayerStatistics::GetAudioPackets() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return audioPackets;
}

int PlayerStatistics::GetVideoFrames() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return videoFrames;
}

int PlayerStatistics::GetAudioBufferMs() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return audioBufferMs;
}

std::string PlayerStatistics::GetResolution() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return resolution;
}

std::string PlayerStatistics::GetVideoCodec() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return videoCodec;
}

std::string PlayerStatistics::GetAudioCodec() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return audioCodec;
}

int PlayerStatistics::GetDroppedFrames() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return droppedFrames;
}

std::string PlayerStatistics::FormatBitrate(
    double bps)
{
    std::ostringstream oss;

    if (bps >= 1000000.0)
    {
        oss
            << std::fixed
            << std::setprecision(1)
            << bps / 1000000.0
            << " Mbps";
    }
    else if (bps >= 1000.0)
    {
        oss
            << std::fixed
            << std::setprecision(0)
            << bps / 1000.0
            << " kbps";
    }
    else
    {
        oss
            << static_cast<int>(bps)
            << " bps";
    }

    return oss.str();
}

void PlayerStatistics::UpdateFPS()
{
    double now =
        SDL_GetTicks() / 1000.0;

    double elapsed =
        now - lastFpsTime;

    // 每 0.5 秒刷新一次实测 FPS
    if (elapsed >= 0.5)
    {
        fps =
            frameCount / elapsed;

        frameCount = 0;

        lastFpsTime = now;
    }
}

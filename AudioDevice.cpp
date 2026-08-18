#include "AudioDevice.h"

#include "Utils/ErrorHandler.h"
#include "Utils/Logger.h"

#include <cstring>

AudioDevice::AudioDevice()
{
}

AudioDevice::~AudioDevice()
{
    Close();
}

bool AudioDevice::Init(
    int sampleRate,
    int channels)
{
    SDL_AudioSpec spec;

    SDL_zero(spec);

    spec.freq = sampleRate;

    spec.format = AUDIO_S16;

    spec.channels = static_cast<Uint8>(channels);

    spec.samples = 1024;

    spec.callback = AudioCallback;

    spec.userdata = this;

    this->sampleRate = sampleRate;

    this->channels = channels;

    // 最大缓冲 = 3 秒音频
    // （v2 缓冲模式 highWater=1950ms，2 秒背压上限会卡在临界；
    //  3 秒与高水位拉开距离，同时背压仍防止解码跑太前）
    pcmQueue.SetMaxBytes(
        static_cast<size_t>(sampleRate) *
        channels *
        2 *
        3);

    device =
        SDL_OpenAudioDevice(
            nullptr,
            0,
            &spec,
            nullptr,
            0);

    if (device == 0)
    {
        ErrorHandler::LogSDL(
            ErrorTag::Audio,
            "SDL_OpenAudioDevice");

        return false;
    }

    SDL_PauseAudioDevice(device, 0);

    paramsReady = true;

    Logger::Info()
        << "[Audio] SDL Audio Device Open Success ("
        << sampleRate
        << " Hz, "
        << channels
        << " ch)"
        << std::endl;

    return true;
}

void AudioDevice::PushPCM(
    const uint8_t* data,
    int size,
    const std::atomic<bool>* abort)
{
    pcmQueue.Push(
        data,
        size,
        abort);
}

int AudioDevice::GetQueuedSize()
{
    return pcmQueue.GetQueuedBytes();
}

double AudioDevice::GetAudioClock() const
{
    return clock.Get();
}

void AudioDevice::ResetClock(
    double baseSeconds)
{
    pcmQueue.Clear();

    clock.Reset(baseSeconds);
}

void AudioDevice::ClearQueue()
{
    pcmQueue.Clear();
}

void AudioDevice::SetPaused(
    bool paused)
{
    if (device)
    {
        SDL_PauseAudioDevice(
            device,
            paused ? 1 : 0);
    }
}

void AudioDevice::SetSpeedFactor(
    double speed)
{
    clock.SetSpeedFactor(speed);
}

void AudioDevice::SetVolume(
    int percent)
{
    volume.SetVolume(percent);
}

int AudioDevice::GetVolume() const
{
    return volume.GetVolume();
}

void AudioDevice::Interrupt()
{
    pcmQueue.Interrupt();
}

void AudioDevice::ResetInterrupt()
{
    pcmQueue.ResetInterrupt();
}

void AudioDevice::Close()
{
    if (device)
    {
        SDL_CloseAudioDevice(device);

        device = 0;
    }

    pcmQueue.Clear();
}

void AudioDevice::AudioCallback(
    void* userdata,
    Uint8* stream,
    int len)
{
    AudioDevice* audio =
        static_cast<AudioDevice*>(userdata);

    if (!audio)
    {
        memset(stream, 0, len);

        return;
    }

    // 从队列填充 PCM
    int filled =
        audio->pcmQueue.Pop(
            stream,
            len);

    // 不足部分填充静音
    if (filled < len)
    {
        memset(
            stream + filled,
            0,
            len - filled);
    }

    // 音量控制
    float factor =
        audio->volume.GetFactor();

    if (factor < 1.0f)
    {
        VolumeController::Apply(
            stream,
            len,
            factor);
    }

    // 更新音频主时钟
    // 媒体时间 += 本次输出时长 * 播放速度
    double bytesPerSec =
        static_cast<double>(audio->sampleRate) *
        audio->channels *
        2;

    if (bytesPerSec > 0.0)
    {
        audio->clock.Update(
            len / bytesPerSec);
    }
}

#include "Audio/AudioMixer.h"

#include <algorithm>

AudioMixer::AudioMixer()
    : volumePercent(100)
{
}

void AudioMixer::SetVolume(
    int percent)
{
    if (percent < 0)
    {
        percent = 0;
    }

    if (percent > 100)
    {
        percent = 100;
    }

    volumePercent.store(percent);
}

int AudioMixer::GetVolume() const
{
    return volumePercent.load();
}

float AudioMixer::GetFactor() const
{
    return volumePercent.load() / 100.0f;
}

void AudioMixer::Apply(
    uint8_t* data,
    int size,
    float factor)
{
    if (!data ||
        size <= 0 ||
        factor >= 1.0f)
    {
        return;   // 音量 100% 时直接跳过
    }

    int16_t* samples =
        reinterpret_cast<int16_t*>(data);

    int count =
        size / 2;    // S16: 每个采样 2 字节

    for (int i = 0; i < count; i++)
    {
        float value =
            samples[i] * factor;

        // clamp 防止溢出
        value =
            std::max(
                -32768.0f,
                std::min(
                    32767.0f,
                    value));

        samples[i] =
            static_cast<int16_t>(value);
    }
}

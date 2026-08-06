#include "Audio/VolumeController.h"

#include <cstring>

VolumeController::VolumeController()
{
}

void VolumeController::SetVolume(
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

int VolumeController::GetVolume() const
{
    return volumePercent.load();
}

float VolumeController::GetFactor() const
{
    return static_cast<float>(
        volumePercent.load()) / 100.0f;
}

void VolumeController::Apply(
    uint8_t* data,
    int size,
    float factor)
{
    if (!data || size <= 0)
    {
        return;
    }

    // 系数 >= 1.0 不需要处理（保持原样）
    if (factor >= 1.0f)
    {
        return;
    }

    // 系数 <= 0 直接静音
    if (factor <= 0.0f)
    {
        std::memset(
            data,
            0,
            size);

        return;
    }

    // 逐采样缩放（S16 小端）
    int16_t* samples =
        reinterpret_cast<int16_t*>(data);

    int count =
        size / 2;

    for (int i = 0; i < count; i++)
    {
        float value =
            static_cast<float>(samples[i]) * factor;

        // clamp 防止溢出
        if (value > 32767.0f)
        {
            value = 32767.0f;
        }
        else if (value < -32768.0f)
        {
            value = -32768.0f;
        }

        samples[i] =
            static_cast<int16_t>(value);
    }
}

#include "Audio/AudioSpeedController.h"

#include <cstring>
#include <algorithm>
#include <cmath>

AudioSpeedController::AudioSpeedController()
{
}

AudioSpeedController::~AudioSpeedController()
{
}

bool AudioSpeedController::Init(
    int sampleRate,
    int channels)
{
    if (sampleRate <= 0 ||
        channels <= 0)
    {
        return false;
    }

    this->sampleRate = sampleRate;

    this->channels = channels;

    Reset();

    return true;
}

void AudioSpeedController::SetSpeed(
    double speed)
{
    // 兼容旧接口：用户倍速
    SetUserSpeed(speed);
}

double AudioSpeedController::GetSpeed() const
{
    return speed.load();
}

void AudioSpeedController::SetUserSpeed(
    double speed)
{
    // 限制范围，防止异常参数
    speed =
        std::max(
            0.25,
            std::min(
                4.0,
                speed));

    userSpeed.store(speed);

    ApplyEffectiveSpeed();
}

void AudioSpeedController::SetChaseSpeed(
    double speed)
{
    // 追帧倍速钳制 1.0 ~ 2.0（防异常参数）
    speed =
        std::max(
            1.0,
            std::min(
                2.0,
                speed));

    chaseSpeed.store(speed);

    ApplyEffectiveSpeed();
}

double AudioSpeedController::GetEffectiveSpeed() const
{
    return speed.load();
}

void AudioSpeedController::ApplyEffectiveSpeed()
{
    // 最终生效速度 = 用户倍速 × 追帧倍速
    double effective =
        userSpeed.load() *
        chaseSpeed.load();

    effective =
        std::max(
            0.25,
            std::min(
                4.0,
                effective));

    speed.store(effective);

    // 输入步长 = 输出步长 * speed
    int newHop =
        static_cast<int>(
            HOP_OUT * effective);

    if (newHop < 1)
    {
        newHop = 1;
    }

    hopIn.store(newHop);
}

int AudioSpeedController::Process(
    const uint8_t* in,
    int inBytes,
    uint8_t* out,
    int outCap)
{
    if (!in ||
        inBytes <= 0)
    {
        return Drain(out, outCap);
    }

    // speed == 1.0 时直通，不做任何处理
    if (std::fabs(speed.load() - 1.0) < 0.001)
    {
        int copy =
            std::min(
                inBytes,
                outCap);

        std::memcpy(out, in, copy);

        // 如果 outCap 不够，剩余数据缓存到 FIFO，下次再取
        if (copy < inBytes)
        {
            const int16_t* samples =
                reinterpret_cast<const int16_t*>(in);

            outFifo.insert(
                outFifo.end(),
                samples + copy / 2,
                samples + inBytes / 2);
        }

        return copy;
    }

    int inFrames =
        inBytes /
        (channels * 2);   // S16: 2 字节

    if (inFrames <= 0)
    {
        return Drain(out, outCap);
    }

    // 追加到输入缓冲
    const int16_t* samples =
        reinterpret_cast<const int16_t*>(in);

    size_t oldSize =
        inputBuf.size();

    inputBuf.resize(
        oldSize +
        static_cast<size_t>(inFrames) * channels);

    std::memcpy(
        inputBuf.data() + oldSize,
        samples,
        static_cast<size_t>(inFrames) * channels * 2);

    // 处理所有完整窗口
    while (
        inputBuf.size() -
            readPos >=
        static_cast<size_t>(WINDOW * channels))
    {
        ProcessWindow();
    }
    // 防止输入缓冲无限增长：
    // 丢弃 readPos 之前已处理完的数据
    if (readPos > 0)
    {
        inputBuf.erase(
            inputBuf.begin(),
            inputBuf.begin() +
                static_cast<ptrdiff_t>(readPos));

        readPos = 0;
    }

    return Drain(out, outCap);
}

void AudioSpeedController::ProcessWindow()
{
    // 取当前窗口（帧为单位）
    const int16_t* win =
        inputBuf.data() +
        readPos;

    const int winFrames = WINDOW;

    // ----------
    // 1. 交叉淡化部分（前 OVERLAP 帧）
    //    prevTail（上一个窗口末尾）淡出
    //    win[0..OVERLAP) 淡入
    // ----------
    if (havePrev)
    {
        for (int i = 0; i < OVERLAP; i++)
        {
            float t =
                static_cast<float>(i) /
                OVERLAP;   // 0 -> 1

            for (int c = 0; c < channels; c++)
            {
                float outSample =
                    prevTail[
                        static_cast<size_t>(i) * channels + c] *
                        (1.0f - t) +
                    win[
                        static_cast<size_t>(i) * channels + c] *
                        t;

                outFifo.push_back(
                    static_cast<int16_t>(
                        std::max(
                            -32768.0f,
                            std::min(
                                32767.0f,
                                outSample))));
            }
        }
    }
    else
    {
        // 第一个窗口：没有上一个窗口，直接输出
        for (int i = 0; i < OVERLAP; i++)
        {
            outFifo.insert(
                outFifo.end(),
                win +
                    static_cast<ptrdiff_t>(i) * channels,
                win +
                    static_cast<ptrdiff_t>(i) * channels +
                    channels);
        }

        havePrev = true;
    }

    // ----------
    // 2. 保存当前窗口末尾 OVERLAP 帧作为下一个窗口的淡化尾
    //    win[OVERLAP..WINDOW)
    // ----------
    prevTail.assign(
        win +
            static_cast<ptrdiff_t>(OVERLAP) * channels,
        win +
            static_cast<ptrdiff_t>(WINDOW) * channels);

    // 3. 输入读取位置前进 Ha 帧
    readPos +=
        static_cast<size_t>(hopIn.load()) * channels;
}

int AudioSpeedController::Flush(
    uint8_t* out,
    int outCap)
{
    return Drain(out, outCap);
}

int AudioSpeedController::PendingBytes() const
{
    return static_cast<int>(
        outFifo.size() * 2);
}

void AudioSpeedController::Reset()
{
    inputBuf.clear();

    outFifo.clear();

    prevTail.clear();

    readPos = 0;

    havePrev = false;
}

int AudioSpeedController::Drain(
    uint8_t* out,
    int outCap)
{
    if (!out ||
        outCap <= 0 ||
        outFifo.empty())
    {
        return 0;
    }

    int copyBytes =
        std::min(
            outCap,
            static_cast<int>(
                outFifo.size() * 2));

    std::memcpy(
        out,
        outFifo.data(),
        copyBytes);

    outFifo.erase(
        outFifo.begin(),
        outFifo.begin() +
            copyBytes / 2);

    return copyBytes;
}

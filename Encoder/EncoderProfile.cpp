// ============================================================
// EncoderProfile.cpp - 编码预设档（9.0）
//
// 数值设计参考：
//   - 均衡档：veryfast（x264 默认档位附近），GOP 2 秒
//   - 低延迟：zerolatency tune 关闭 VBV 缓存，GOP 1 秒，
//     允许 B 帧（多数解码器可接受 1~2 个）
//   - 超低延迟：禁 B 帧 + ultrafast + VBV 收紧到目标码率的
//     1.2 倍，码率波动小（适合推流 / 监控）
//   - 高质量：slow + GOP 4 秒 + 2 倍码率余量
// ============================================================

#include "Encoder/EncoderProfile.h"

#include <algorithm>
#include <cctype>

EncoderProfile EncoderProfile::Balanced()
{
    EncoderProfile p;

    p.name = "balanced";

    p.preset = "veryfast";

    // 其余字段走默认（GOP / B 帧 / VBV 由调用方或 Init 决定）

    return p;
}

EncoderProfile EncoderProfile::LowLatency()
{
    EncoderProfile p;

    p.name = "low-latency";

    p.preset = "fast";

    p.tune = "zerolatency";

    p.gopSize = 0;          // 1 秒由调用方换算（需知道帧率）

    p.bFrames = 2;

    p.lowLatency = true;

    return p;
}

EncoderProfile EncoderProfile::UltraLowLatency()
{
    EncoderProfile p;

    p.name = "ultra-low-latency";

    p.preset = "ultrafast";

    p.tune = "zerolatency";

    p.bFrames = 0;

    p.vbvBufferKbps = 0;    // 由调用方按目标码率设定

    p.lowLatency = true;

    p.cbr = true;

    return p;
}

EncoderProfile EncoderProfile::HighQuality()
{
    EncoderProfile p;

    p.name = "high-quality";

    p.preset = "slow";

    p.gopSize = 0;          // 4 秒由调用方换算

    p.bFrames = 4;

    p.maxBitrateKbps = 0;   // 2 倍余量由调用方按码率设定

    return p;
}

EncoderProfile EncoderProfile::FromName(
    const std::string& name)
{
    std::string lower = name;

    std::transform(
        lower.begin(),
        lower.end(),
        lower.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(
                std::tolower(c));
        });

    if (lower == "low-latency" ||
        lower == "lowlatency" ||
        lower == "low")
    {
        return LowLatency();
    }

    if (lower == "ultra-low-latency" ||
        lower == "ultralowlatency" ||
        lower == "ultra")
    {
        return UltraLowLatency();
    }

    if (lower == "high-quality" ||
        lower == "highquality" ||
        lower == "high" ||
        lower == "quality")
    {
        return HighQuality();
    }

    return Balanced();
}

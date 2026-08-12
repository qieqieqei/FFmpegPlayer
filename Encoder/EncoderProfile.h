#pragma once

// ============================================================
// EncoderProfile - 编码预设档（9.0，评审意见）
//
// 职责：以"档位"为单位描述编码参数组合，供 VideoEncoder
//       在 avcodec_open2 之前应用（纯数据，不依赖 FFmpeg，
//       便于配置序列化 / 单元测试）。
//
// 内置档位：
//   - Balanced        ：均衡（默认，veryfast + 常规 GOP）
//   - LowLatency      ：低延迟（fast + zerolatency + 短 GOP）
//   - UltraLowLatency ：超低延迟（ultrafast + zerolatency +
//                       禁 B 帧 + VBV 收紧 + CBR 倾向）
//   - HighQuality     ：高质量（slow + 大 GOP + 高码率余量）
//
// 各字段含义与 VideoEncoder::SetXxx 对应；0 / 空 / -1 表示
// "沿用编码器默认或调用方既有参数"。
// ============================================================

#include <string>

struct EncoderProfile
{
    std::string name;           // 档位名（日志 / 配置用）
    std::string preset;         // x264: ultrafast..veryslow；nvenc: ll/llhq/p1-p7
    std::string tune;           // x264: zerolatency/film/...；空 = 不设置
    std::string profile;        // high/main/baseline；空 = 编码器默认
    std::string level;          // "4.1" 等；空 = 编码器默认
    int gopSize = 0;            // 关键帧间隔（帧数；0 = 自动 2 秒）
    int bFrames = -1;           // B 帧数（-1 = 编码器默认；0 = 禁用）
    int bitrateKbps = 0;        // 目标码率（0 = 沿用 Init 参数）
    int maxBitrateKbps = 0;     // 最大码率 / VBV max（0 = 不设置）
    int vbvBufferKbps = 0;      // VBV 缓冲（0 = 不设置）
    int threads = 0;            // 编码线程数（0 = 自动）
    bool lowLatency = false;    // 编码器级低延迟（zerolatency / ll）
    bool cbr = false;           // 恒定码率（nal-hrd=cbr / rc=cbr）

    // ---------- 内置档位工厂 ----------

    static EncoderProfile Balanced();

    static EncoderProfile LowLatency();

    static EncoderProfile UltraLowLatency();

    static EncoderProfile HighQuality();

    // 按名取档（不区分大小写；未知返回 Balanced）
    static EncoderProfile FromName(
        const std::string& name);
};

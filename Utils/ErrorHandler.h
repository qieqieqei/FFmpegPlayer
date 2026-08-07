#pragma once

// ============================================================
// ErrorHandler - 统一错误系统（5.8）
//
// 所有模块的错误统一从这里输出：
//
//   [FFmpeg]   FFmpeg 库错误（av_strerror）
//   [SDL]      SDL 错误（SDL_GetError）
//   [Decoder]  解码器错误
//   [Audio]    音频模块错误
//   [Video]    视频模块错误
//   [Sync]     音视频同步错误
//   [Queue]    队列错误
//   [Screenshot] 截图错误
//   [Player]   播放器整体错误
//
// 用法：
//   ErrorHandler::Log(ErrorTag::Decoder, "Init failed");
//   ErrorHandler::LogFFmpeg(ErrorTag::Decoder, "avcodec_open2", ret);
//   ErrorHandler::LogSDL(ErrorTag::Video, "SDL_CreateRenderer");
// ============================================================

#include <string>

enum class ErrorTag
{
    General,     // 通用
    FFmpeg,      // FFmpeg
    SDL,         // SDL
    Decoder,     // 解码器
    Audio,       // 音频
    Video,       // 视频
    Sync,        // 同步
    Queue,       // 队列
    Screenshot,  // 截图
    Player,      // 播放器
    Encoder,     // 编码器
    Muxer,       // 封装
    Filter,      // 滤镜
    Network      // 网络
};

class ErrorHandler
{
public:

    // 输出带标签的普通错误信息
    static void Log(
        ErrorTag tag,
        const std::string& message);

    // 输出带标签的错误信息 + FFmpeg 错误码描述
    // context: 出错位置描述，例如 "avcodec_open2"
    // ret:     FFmpeg 返回的错误码
    static void LogFFmpeg(
        ErrorTag tag,
        const std::string& context,
        int ret);

    // 输出带标签的错误信息 + SDL 错误描述
    static void LogSDL(
        ErrorTag tag,
        const std::string& context);

    // 检查结果，失败时自动输出错误
    // 返回传入的 ok，方便直接 if (!ErrorHandler::Check(...))
    static bool Check(
        bool ok,
        ErrorTag tag,
        const std::string& context);

    // 把 FFmpeg 错误码转成可读字符串（内部使用 av_strerror）
    static std::string FFmpegError(int ret);

    // 获取 SDL 错误字符串
    static std::string SDLError();

    // 把标签转成前缀字符串，例如 [FFmpeg]
    static const char* TagName(ErrorTag tag);
};

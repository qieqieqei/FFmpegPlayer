#pragma once

// ============================================================
// PlayerConfig - 播放器配置（7.11）
//
// 对应 player.json，避免代码写死：
//
//   {
//     "window_width"    : 1280,
//     "window_height"   : 720,
//     "volume"          : 100,
//     "playback_speed"  : 1.0,
//     "fullscreen"      : false,
//     "default_url"     : "",
//     "log_debug"       : false,
//     "log_file"        : ""
//   }
//
// 由 ConfigManager 加载；文件缺失时使用默认值。
// ============================================================

#include <string>

struct PlayerConfig
{
    // 窗口尺寸（当前窗口按视频尺寸自适应，预留 UI 阶段使用）
    int windowWidth = 1280;

    int windowHeight = 720;

    // 音量 0~100
    int volume = 100;

    // 播放速度（0.5 / 1.0 / 1.5 / 2.0）
    double playbackSpeed = 1.0;

    // 启动即全屏
    bool fullscreen = false;

    // 无命令行参数时默认播放的 URL（支持本地文件与网络流）
    std::string defaultUrl;

    // 日志级别 / 日志文件
    bool logDebug = false;

    std::string logFile;
};

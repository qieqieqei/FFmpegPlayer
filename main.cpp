// main.cpp - 播放器入口
//
// 用法：
//   FFmpeg_text_claw.exe [文件1] [文件2] ...
//   FFmpeg_text_claw.exe -v [文件...]      # DEBUG 级别日志
//   FFmpeg_text_claw.exe --log-file xxx.log [文件...]  # 同时写日志文件
//   FFmpeg_text_claw.exe --record [文件...]   # 启动即录制（record_<时间戳>.flv）
//   FFmpeg_text_claw.exe --hls [文件...]      # 启动即 HLS 输出（hls_out/）
//   FFmpeg_text_claw.exe --push [文件...]     # 启动即 RTMP 推流（stream.json 的 rtmp_url）
//   不带参数时播放默认测试视频（或 player.json 的 default_url）
//   多个文件会加入播放列表（6.8），用 [ / ] 切换

#define SDL_MAIN_HANDLED

#include <SDL.h>

#include <cstring>
#include <string>

#include "Player.h"
#include "Config/ConfigManager.h"
#include "Utils/Logger.h"

// 默认测试视频（无配置文件 / 无命令行参数时使用）
static const char* kDefaultVideo =
    R"(D:\application\visual studio\product\FFmpeg_text_claw\a4c277.mp4)";

int main(
    int argc,
    char* argv[])
{
    // ---------- 命令行参数：-v / --log-file 先解析 ----------

    LogLevel level = LogLevel::Info;

    std::string logFile;

    bool cliRecord = false;

    bool cliHls = false;

    bool cliPush = false;

    int firstFile = -1;  // 第一个非选项参数（文件）下标

    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "-v") == 0)
        {
            level = LogLevel::Debug;
        }
        else if (std::strcmp(argv[i], "--log-file") == 0 &&
                 i + 1 < argc)
        {
            logFile = argv[i + 1];

            i++;  // 跳过日志文件名
        }
        else if (std::strcmp(argv[i], "--record") == 0)
        {
            // 启动即录制（record_<时间戳>.flv）
            cliRecord = true;
        }
        else if (std::strcmp(argv[i], "--hls") == 0)
        {
            // 启动即 HLS 输出（hls_out/）
            cliHls = true;
        }
        else if (std::strcmp(argv[i], "--push") == 0)
        {
            // 启动即 RTMP 推流（stream.json 的 rtmp_url）
            cliPush = true;
        }
        else if (firstFile < 0)
        {
            // 第一个非选项参数 = 文件起点
            firstFile = i;
        }
    }

    // ---------- 配置（7.11）：先加载，用于决定日志级别 ----------

    ConfigManager config;

    config.Load(".");

    const PlayerConfig& pcfg =
        config.GetPlayerConfig();

    // 未显式指定 -v 时，跟随配置文件 log_debug
    if (level == LogLevel::Info &&
        pcfg.logDebug)
    {
        level = LogLevel::Debug;
    }

    // 未显式指定 --log-file 时，跟随配置文件 log_file
    if (logFile.empty() &&
        !pcfg.logFile.empty())
    {
        logFile = pcfg.logFile;
    }

    Logger::Init(
        level,
        logFile);

    Player player;

    // 加载并应用配置（音量 / 速度；Init 内部还会用网络参数）
    player.LoadConfig();

    // ---------- 播放列表（6.8）：多文件 / 单文件 / 默认 ----------

    // 注意：选项（--record/--hls/--push/-v/--log-file）可能出现在文件之后，
    // 构建列表时必须跳过，否则会被当成播放文件（EOF 后自动播到选项名而失败）
    if (firstFile >= 0 && firstFile < argc)
    {
        for (int i = firstFile; i < argc; i++)
        {
            if (std::strcmp(argv[i], "--record") == 0 ||
                std::strcmp(argv[i], "--hls") == 0 ||
                std::strcmp(argv[i], "--push") == 0 ||
                std::strcmp(argv[i], "-v") == 0)
            {
                continue;
            }

            if (std::strcmp(argv[i], "--log-file") == 0)
            {
                i++;  // 跳过日志文件名

                continue;
            }

            Logger::Info()
                << "[Main] Add : "
                << argv[i]
                << std::endl;

            player.AddToPlaylist(argv[i]);
        }
    }
    else
    {
        // 无命令行参数：优先配置文件 default_url，其次内置默认
        const std::string& defUrl =
            player.GetConfigManager()->
                GetPlayerConfig().defaultUrl;

        if (!defUrl.empty())
        {
            Logger::Info()
                << "[Main] Add (config default_url) : "
                << defUrl
                << std::endl;

            player.AddToPlaylist(defUrl);
        }
        else
        {
            Logger::Info()
                << "[Main] Add : "
                << kDefaultVideo
                << std::endl;

            player.AddToPlaylist(kDefaultVideo);
        }
    }

    // 8.14 loop: single file -> expand with folder siblings
    player.ExpandPlaylistWithSiblings();

    // ---------- 打开第一个文件 ----------

    Logger::Info()
        << "[Main] Open : "
        << player.GetCurrentPath()
        << std::endl;

    if (!player.Init(
        player.GetCurrentPath().c_str()))
    {
        Logger::Error()
            << "[Main] Init failed"
            << std::endl;

        return -1;
    }

    // ---------- 启动即输出的 CLI 开关（录制 / HLS / 推流） ----------

    if (cliRecord)
    {
        player.ToggleRecording();
    }

    if (cliHls)
    {
        player.ToggleHLS();
    }

    if (cliPush)
    {
        player.TogglePushing();
    }

    // CLI 输出模式：EOF 后停留 3 秒自动退出（测试/批处理友好）
    if (cliRecord || cliHls || cliPush)
    {
        player.SetAutoQuitOnEof(true);
    }

    // ---------- 按键提示 ----------

    Logger::Info()
        << "----------------------------------------"
        << std::endl;

    Logger::Info()
        << "Space  Pause / Resume"
        << std::endl;

    Logger::Info()
        << "R      Cycle Speed (0.5x/1x/1.5x/2x)"
        << std::endl;

    Logger::Info()
        << "Left/Right  Seek -5s / +5s"
        << std::endl;

    Logger::Info()
        << "[ / ]  Previous / Next (playlist)"
        << std::endl;

    Logger::Info()
        << "T      Toggle Subtitle"
        << std::endl;

    Logger::Info()
        << "S/J    Screenshot PNG / JPG"
        << std::endl;

    Logger::Info()
        << "N      Frame Step (paused)"
        << std::endl;

    Logger::Info()
        << "+/-    Volume +10 / -10"
        << std::endl;

    Logger::Info()
        << "F      Fullscreen"
        << std::endl;

    Logger::Info()
        << "ESC/Q  Exit"
        << std::endl;

    Logger::Info()
        << "C      Toggle Recording (FLV)"
        << std::endl;

    Logger::Info()
        << "P      Toggle RTMP Push"
        << std::endl;

    Logger::Info()
        << "H      Toggle HLS Output"
        << std::endl;

    Logger::Info()
        << "----------------------------------------"
        << std::endl;

    player.Run();

    player.Close();

    Logger::Info()
        << "[Main] Exit"
        << std::endl;

    return 0;
}

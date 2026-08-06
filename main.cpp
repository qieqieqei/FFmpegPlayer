// main.cpp - 播放器入口
//
// 用法：
//   FFmpeg_text_claw.exe [文件1] [文件2] ...
//   FFmpeg_text_claw.exe -v [文件...]      # DEBUG 级别日志
//   FFmpeg_text_claw.exe --log-file xxx.log [文件...]  # 同时写日志文件
//   不带参数时播放默认测试视频
//   多个文件会加入播放列表（6.8），用 [ / ] 切换

#define SDL_MAIN_HANDLED

#include <SDL.h>

#include <cstring>
#include <string>

#include "Player.h"
#include "Utils/Logger.h"

// 默认测试视频
static const char* kDefaultVideo =
    R"(D:\FFmpeg\ffmpeg\test_audio.mp4)";

int main(
    int argc,
    char* argv[])
{
    // ---------- 命令行参数：-v / --log-file 先解析 ----------

    LogLevel level = LogLevel::Info;

    std::string logFile;

    int firstFile = 1;

    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "-v") == 0)
        {
            level = LogLevel::Debug;

            firstFile = i + 1;
        }
        else if (std::strcmp(argv[i], "--log-file") == 0 &&
                 i + 1 < argc)
        {
            logFile = argv[i + 1];

            firstFile = i + 2;

            i++;  // 跳过日志文件名
        }
    }

    Logger::Init(
        level,
        logFile);

    Player player;

    // ---------- 播放列表（6.8）：多文件 / 单文件 / 默认 ----------

    if (firstFile < argc)
    {
        for (int i = firstFile; i < argc; i++)
        {
            Logger::Info()
                << "[Main] Add : "
                << argv[i]
                << std::endl;

            player.AddToPlaylist(argv[i]);
        }
    }
    else
    {
        Logger::Info()
            << "[Main] Add : "
            << kDefaultVideo
            << std::endl;

        player.AddToPlaylist(kDefaultVideo);
    }

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
        << "----------------------------------------"
        << std::endl;

    player.Run();

    player.Close();

    Logger::Info()
        << "[Main] Exit"
        << std::endl;

    return 0;
}

#include "Event.h"

#include "Player.h"

#include <iostream>

// ============================================================
// 事件处理
// ============================================================

void HandleEvent(
    bool& quit,
    Player* player)
{
    SDL_Event event;

    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_QUIT:
        {
            // 关闭窗口：退出
            quit = true;

            break;
        }

        case SDL_KEYDOWN:
        {
            SDL_Keycode key =
                event.key.keysym.sym;

            switch (key)
            {
            case SDLK_SPACE:
            {
                // 暂停 / 继续
                player->TogglePause();

                break;
            }

            case SDLK_ESCAPE:
            {
                // 全屏时：回到窗口
                if (player->IsFullScreen())
                {
                    player->ToggleFullScreen();
                }
                else
                {
                    // 窗口时：退出
                    quit = true;
                }

                break;
            }

            case SDLK_f:
            {
                // 切换全屏
                player->ToggleFullScreen();

                break;
            }

            case SDLK_LEFT:
            {
                // 后退 5 秒
                player->RequestSeek(
                    player->GetCurrentTime() - 5.0);

                break;
            }

            case SDLK_RIGHT:
            {
                // 前进 5 秒
                player->RequestSeek(
                    player->GetCurrentTime() + 5.0);

                break;
            }

            case SDLK_r:
            {
                // 循环切换倍速：0.5 -> 1 -> 1.5 -> 2 -> 0.5
                double speed =
                    player->GetPlaybackSpeed();

                if (speed < 0.75)
                {
                    speed = 1.0;
                }
                else if (speed < 1.25)
                {
                    speed = 1.5;
                }
                else if (speed < 1.75)
                {
                    speed = 2.0;
                }
                else
                {
                    speed = 0.5;
                }

                player->SetPlaybackSpeed(speed);

                break;
            }

            case SDLK_s:
            {
                // 截图 PNG
                player->TakeScreenshot("png");

                break;
            }

            case SDLK_j:
            {
                // 截图 JPG
                player->TakeScreenshot("jpg");

                break;
            }

            case SDLK_n:
            {
                // 帧步进（暂停时逐帧播放）
                player->RequestFrameStep();

                break;
            }

            case SDLK_EQUALS:
            case SDLK_PLUS:
            {
                // 音量 +10
                player->SetVolume(
                    player->GetVolume() + 10);

                break;
            }

            case SDLK_MINUS:
            {
                // 音量 -10
                player->SetVolume(
                    player->GetVolume() - 10);

                break;
            }

            case SDLK_q:
            {
                // 退出
                quit = true;

                break;
            }

            case SDLK_LEFTBRACKET:
            {
                // 上一首
                player->PlayPrevious();

                break;
            }

            case SDLK_RIGHTBRACKET:
            {
                // 下一首
                player->PlayNext();

                break;
            }

            case SDLK_t:
            {
                // 字幕开 / 关
                player->ToggleSubtitle();

                break;
            }

            default:
                break;
            }

            break;
        }

        default:
            break;
        }
    }
}

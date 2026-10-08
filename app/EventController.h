#pragma once

#include <SDL.h>

#include "core/IInputHandler.h"

class Player;

// 处理 SDL 事件（渲染循环内每帧调用）
//
// 按键：
//   Space   暂停 / 继续
//   ESC     全屏 -> 窗口；窗口状态下退出
//   F       切换全屏
//   LEFT    后退 5 秒
//   RIGHT   前进 5 秒
//   R       循环切换倍速 0.5x / 1x / 1.5x / 2x
//   S       截图 PNG
//   J       截图 JPG
//   N       帧步进（暂停时）
//   + / =   音量 +10
//   -       音量 -10
//   Q       退出
class EventController : public IInputHandler
{
public:

    explicit EventController(
        Player* player);

    void HandleEvents(
        bool& quit) override;

private:

    Player* player = nullptr;
};

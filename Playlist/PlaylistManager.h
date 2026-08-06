#pragma once

// ============================================================
// PlaylistManager - 播放列表管理（6.8）
//
// 功能：
//   - 多文件播放列表（vector + 当前索引）
//   - 上一首 / 下一首切换
//   - 自动播放（EOF 时自动切下一首）
//
// 与 Player 的配合：
//   Player 持有 PlaylistManager
//   main 把命令行参数逐个 AddMedia
//   播放结束时 Player 调 Next() + SwitchMedia()
// ============================================================

#include <string>
#include <vector>

class PlaylistManager
{
public:

    PlaylistManager();

    // 添加一个文件到列表末尾
    void AddMedia(
        const std::string& path);

    // 清空列表
    void Clear();

    // 切到下一首（列表末尾则回到开头）
    // 返回是否切换成功（空列表返回 false）
    bool Next();

    // 切到上一首（列表开头则回到末尾）
    // 返回是否切换成功（空列表返回 false）
    bool Previous();

    // 是否有下一首（含回到开头的循环）
    bool HasNext() const;

    bool HasPrevious() const;

    // 当前文件路径（空列表返回空串）
    const std::string& GetCurrent() const;

    // 设置当前索引（越界自动夹紧）
    void SetIndex(
        size_t index);

    size_t GetIndex() const;

    size_t Count() const;

    // 自动播放开关（EOF 自动切下一首）
    void SetAutoPlay(
        bool on);

    bool IsAutoPlay() const;

private:

    std::vector<std::string> items;   // 文件列表

    size_t index = 0;                 // 当前索引

    bool autoPlay = true;             // 自动播放
};

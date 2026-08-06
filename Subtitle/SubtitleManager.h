#pragma once

// ============================================================
// SubtitleManager - 字幕系统（6.9）
//
// 支持格式：
//   .srt  - 标准 SubRip
//   .ass  - Advanced SubStation Alpha（基础解析）
//
// 流程：
//   subtitle file
//        |
//        v
//      parse（按时间排序）
//        |
//        v
//   timestamp match（GetTextAt）
//        |
//        v
//   OSD Render（Player 每帧取当前字幕文本交给 OSDManager）
//
// 时间单位统一用秒（double），内部按时间排序存储
// ============================================================

#include <string>
#include <vector>

class SubtitleManager
{
public:

    // 一条字幕
    struct Entry
    {
        double start = 0.0;    // 开始时间（秒）

        double end = 0.0;      // 结束时间（秒）

        std::string text;      // 文本（多行合并为一行）
    };

    SubtitleManager();

    // 加载字幕文件（自动按扩展名选择解析器）
    // 返回 true = 解析出至少一条字幕
    bool Load(
        const std::string& path);

    // 取指定时刻的字幕文本，没有则返回空字符串
    std::string GetTextAt(
        double seconds) const;

    // 清空所有字幕
    void Clear();

    // 是否已加载字幕
    bool IsLoaded() const;

    // 字幕条目数量
    int Count() const;

    // 开 / 关字幕显示
    void SetEnabled(
        bool enabled);

    bool IsEnabled() const;

private:

    // ---------- 解析器 ----------

    bool ParseSrt(
        const std::string& content);

    bool ParseAss(
        const std::string& content);

    // 解析 SRT 时间 "HH:MM:SS,mmm" -> 秒
    static bool ParseSrtTime(
        const std::string& token,
        double& outSeconds);

    // 解析 ASS 时间 "H:MM:SS.cc" -> 秒
    static bool ParseAssTime(
        const std::string& token,
        double& outSeconds);

    // 去除 ASS 样式标签 {\...}，\N 换行转空格
    static std::string CleanAssText(
        const std::string& text);

    // 按开始时间排序
    static void SortEntries(
        std::vector<Entry>& entries);

    std::vector<Entry> entries;   // 字幕条目（按开始时间排序）

    bool loaded = false;          // 是否已加载

    bool enabled = true;          // 是否显示
};

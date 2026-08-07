#pragma once

// ============================================================
// FileInput - 本地文件输入（7.1）
//
// 继承 InputSource，负责本地文件：
//   - 普通路径：  test.mp4
//   - file:// 前缀：file://test.mp4
//
// 支持封装格式：MP4 / MKV / AVI / TS 等
// （具体格式由 FFmpeg 按扩展名 / 内容探测决定）
// ============================================================

#include "Input/InputSource.h"

class FileInput : public InputSource
{
public:

    FileInput();

    ~FileInput() override;

    // 打开本地文件（自动剥离 file:// 前缀）
    bool Open(
        const std::string& url) override;

    // 关闭并释放
    void Close() override;
};

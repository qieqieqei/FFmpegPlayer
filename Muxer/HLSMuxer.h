#pragma once

// ============================================================
// HLSMuxer - HLS 封装器（7.6）
//
// 把编码后的音视频流切片输出为 HLS 直播/点播：
//
//   index.m3u8             播放列表
//   segment001.ts          分段 1（默认 4 秒）
//   segment002.ts          分段 2
//   ...
//
// 实现：直接复用 FFmpeg 原生 hls muxer
//   （hls_time / hls_list_size / hls_flags=delete_segments）
//   —— 分段、索引更新、旧段清理全部由 muxer 内部完成，
//      因此不再需要单独的 HLSSegmenter 类（合并职责）。
//
// 用法：
//   HLSMuxer hls;
//   hls.SetSegmentDuration(4);   // 每段秒数
//   hls.SetListSize(6);          // 播放列表保留段数（0=全部）
//   hls.SetDeleteSegments(true); // 自动删除过期段
//   hls.OpenOutput("index.m3u8");   // 或 "dir/index.m3u8"
//   hls.AddVideoStream(...); AddAudioStream(...);
//   hls.WriteHeader();
//   hls.WritePacket(pkt); ...
//   hls.WriteTrailer();
// ============================================================

#include "Muxer/Muxer.h"

class HLSMuxer : public Muxer
{
public:

    HLSMuxer();

    // 打开输出（m3u8 路径，分段默认写同目录）
    bool OpenOutput(
        const std::string& url) override;

    // 关闭并释放
    void Close() override;

    // ---------- HLS 参数（OpenOutput 之前设置） ----------

    // 每段时长（秒），默认 4
    void SetSegmentDuration(
        double seconds);

    // 播放列表保留段数，0 = 全部保留（点播），默认 6
    void SetListSize(
        int size);

    // 自动删除过期分段，默认 true（直播场景必须开启，
    // 否则磁盘会被旧段占满）
    void SetDeleteSegments(
        bool enabled);

    // 自定义分段文件名模式（默认 "segment%03d.ts"）
    // 注意：%d 前面的 0 表示补零宽度（%03d = 3 位）
    void SetSegmentFilenamePattern(
        const std::string& pattern);

private:

    const char* GetFormatName() const override;

    double segmentDuration = 4.0;   // 每段秒数

    int listSize = 6;               // 列表保留段数

    bool deleteSegments = true;     // 删除过期段

    std::string segmentPattern = "segment%03d.ts";
};

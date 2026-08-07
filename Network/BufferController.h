#pragma once

// ============================================================
// BufferController - 网络播放缓冲控制（7.3）
//
// 背景：
//   本地：读取速度 > 播放速度，队列天然积压
//   网络：网络速度随时变化，可能出现：
//     - 缓冲不足（卡顿）：需要暂停消费，等待数据积压
//     - 缓冲充足：恢复播放
//
// 职责：根据缓冲水位决定"是否该缓冲 / 是否已足够"。
//
// 水位模型：
//
//   0         lowWater          highWater        target
//   |----------|------------------|---------------|
//   |  饥饿    |   缓冲中(不足)    |   正常播放     |
//   | NeedBuffer=true             | IsEnough=true |
//
//   NeedBuffer() : 低于低水位（需要缓冲）
//   IsEnough()   : 高于高水位（缓冲充足，可以播放）
//
// 直播 / 点播差异：
//   直播：低延迟优先，目标缓冲小（默认 300ms，由 stream.json 配置）
//   点播：流畅优先，目标缓冲大（默认 2000ms）
//
// 用法（渲染线程每帧调用）：
//   bufferController->SetLive(demuxer->IsLive());
//   bufferController->Update(bufferedMs);
//   if (bufferController->NeedBuffer()) { OSD 显示"缓冲中..." }
// ============================================================

class BufferController
{
public:

    BufferController();

    // 设置播放类型（直播 / 点播），影响水位
    // live = true  -> 目标 300ms（低延迟）
    // live = false -> 目标 2000ms（流畅优先）
    void SetLive(
        bool live);

    bool IsLive() const;

    // 手动设置目标缓冲（毫秒），覆盖默认值
    void SetTargetBufferMs(
        int ms);

    // 设置水位（毫秒）
    void SetWatermarks(
        int lowMs,
        int highMs);

    // 每帧更新当前缓冲时长（毫秒），内部结算状态
    void Update(
        double bufferedMs);

    // 缓冲不足：需要继续缓冲（暂停播放等待数据）
    bool NeedBuffer() const;

    // 缓冲充足：可以正常播放
    bool IsEnough() const;

    // 当前缓冲时长（毫秒）
    double GetBufferedMs() const;

    // 目标缓冲（毫秒）
    int GetTargetMs() const;

    // 是否刚进入"缓冲中"状态（供 OSD 提示，一次性查询）
    bool ConsumeBufferingEvent();

private:

    bool live = true;            // 是否直播

    int targetMs = 300;          // 目标缓冲（毫秒）

    int lowWaterMs = 150;        // 低水位（毫秒）

    int highWaterMs = 400;       // 高水位（毫秒）

    double bufferedMs = 0.0;     // 当前缓冲时长（毫秒）

    bool wasBuffering = false;   // 上一帧是否在缓冲

    bool bufferingEvent = false; // 状态跳变事件（进入缓冲）
};

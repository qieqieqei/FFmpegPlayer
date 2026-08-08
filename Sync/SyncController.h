#pragma once

// ============================================================
// SyncController - 音视频同步控制器（5.1 / 8.1 重构）
//
// 同步策略：主时钟（Master Clock）
//
//   MasterClock（自动选择）
//     +-- AudioClock（音频可用时：音频主时钟）
//     +-- VideoClock（无音频时：视频主时钟）
//
//   videoPts ----+
//                +----> FrameScheduler.ComputeDelay() -> 渲染延迟
//   masterTime --+
//
//   delay > 0 : 视频超前，需要等待 delay 秒再渲染
//   delay < 0 : 视频落后，交给 DropController 决定是否丢帧
//
// 8.1 重构：拆出四个独立组件，本类作为门面（Facade）组合：
//   - VideoClock     视频时钟（无音频主时钟 / 帧时刻记录）
//   - MasterClock    主时钟选择器（音频优先，无音频用视频）
//   - FrameScheduler 帧调度器（延迟计算 / 钳制 / 分片等待）
//   - DropController 丢帧控制器（丢帧判定 + 统计）
// ============================================================

#include "Sync/VideoClock.h"
#include "Sync/MasterClock.h"
#include "Sync/FrameScheduler.h"
#include "Sync/DropController.h"

class AudioClock;

class SyncController
{
public:

    SyncController();

    // ---------- 时钟源绑定（Player 在媒体打开/释放时调用） ----------

    // 绑定音频主时钟（音频设备创建后调用；nullptr = 无音频）
    void SetAudioClock(
        AudioClock* clock);

    // 当前主时钟时间（秒）——同步基准
    double GetMasterTime() const;

    // ---------- 帧调度 ----------

    // 计算视频渲染延迟
    // videoPts: 当前视频帧的时间戳（秒）
    //
    // 返回：
    //   > 0 : 视频帧超前，延迟这么多秒再显示
    //   < 0 : 视频帧落后，落后超过阈值时应丢帧
    double GetVideoDelay(
        double videoPts);

    // 延迟钳制（Seek/断流恢复等异常跳变保护）
    double ClampDelay(
        double delay,
        bool* isAbnormal = nullptr) const;

    // 无音频模式帧间隔（frameDuration / speed）
    double GetFrameInterval(
        double frameDuration) const;

    // 分片等待：返回本次应 sleep 的毫秒数
    int NextWaitMs(
        double delay) const;

    // ---------- 丢帧控制 ----------

    // 是否应丢帧（内部含连续判定 + 冷却防抖）
    bool ShouldDrop(
        double delay) const;

    // 登记丢了一帧（渲染线程丢弃后调用）
    void OnFrameDropped();

    // 累计丢帧数（DropController）
    int GetDropCount() const;

    // 丢帧阈值（秒）
    double GetDropThreshold() const;

    // ---------- 视频时钟 ----------

    // 渲染线程每帧记录 PTS（无音频时作为主时钟推进基准）
    void UpdateVideoClock(
        double pts);

    // 视频时钟当前值（秒）
    double GetVideoClockTime() const;

    // ---------- 组件访问（调试 / 进阶使用） ----------

    VideoClock* GetVideoClock();

    MasterClock* GetMasterClock();

    FrameScheduler* GetFrameScheduler();

    DropController* GetDropController();

    // 重置同步状态（Seek 时调用）
    void Reset();

private:

    VideoClock videoClock;          // 视频时钟

    MasterClock masterClock;        // 主时钟选择器

    FrameScheduler frameScheduler;  // 帧调度器

    DropController dropController;  // 丢帧控制器
};

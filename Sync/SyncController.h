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
//
// 8.5 直播模式（LiveClock）：
//   Player 打开直播流后调用 SetLiveMode(true)：
//   - ShouldDrop 交给 LiveClock（超前 / 落后都丢，追最新画面）
//   - NextWaitMs 恒返回 0（直播不等待，最低延迟）
//   点播保持原策略（PTS 正常对齐，等待 + 仅落后丢帧）
// ============================================================

#include "Sync/VideoClock.h"
#include "Sync/MasterClock.h"
#include "Sync/FrameScheduler.h"
#include "Sync/DropController.h"
#include "Sync/LiveClock.h"

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

    // v2 Metrics: A/V sync deviation (ms) = video clock - master clock
    // > 0 video ahead, < 0 audio ahead
    int GetAvSyncMs() const;

    // ---------- 帧调度 ----------

    // 计算视频渲染延迟
    // videoPts: 当前视频帧的时间戳（秒）
    // frameDuration: 当前帧显示时长（秒，1/fps 或相邻帧 PTS 差）
    //
    // 返回：
    //   > 0 : 视频帧超前，延迟这么多秒再显示
    //   < 0 : 视频帧落后，落后超过阈值时应丢帧
    //
    // 8.4（评审五）：内部按 ffplay compute_target_delay 规则
    // 用视频时钟偏差微调：领先放慢（delay 增）、落后追赶（delay 减）
    double GetVideoDelay(
        double videoPts,
        double frameDuration);

    // 延迟钳制（Seek/断流恢复等异常跳变保护）
    double ClampDelay(
        double delay,
        bool* isAbnormal = nullptr) const;

    // 无音频模式帧间隔（frameDuration / speed）
    double GetFrameInterval(
        double frameDuration) const;

    // 分片等待：返回本次应 sleep 的毫秒数
    // 直播模式恒返回 0（不等待，最低延迟）
    int NextWaitMs(
        double delay) const;

    // ---------- 直播模式（8.5） ----------

    // 切换直播 / 点播同步策略（Player 在媒体打开时调用）
    //   live = true : 直播策略——ShouldDrop 交给 LiveClock，
    //                 超前 / 落后都丢帧；NextWaitMs 恒 0
    //   live = false: 点播策略——PTS 正常对齐（原行为）
    void SetLiveMode(
        bool live);

    // 稳定缓冲模式（v2，直播时调用）：
    //   stable = true  : 关闭"超前丢帧"（积压是缓冲要攒的水位，
    //                    不能一边 Buffer 一边丢 Buffer），
    //                    落后丢帧阈值放大到 2500ms 作兜底
    //   stable = false : 低延迟模式（恢复 100ms / 50ms 旧行为）
    void SetBufferStableMode(
        bool stable);

    // 是否直播模式
    bool IsLiveMode() const;

    // 直播时钟访问（调试 / 进阶使用）
    LiveClock* GetLiveClock();

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

    DropController dropController;  // 丢帧控制器（点播）

    LiveClock liveClock;            // 直播丢帧控制器（8.5）

    bool liveMode = false;          // 直播模式（8.5）
};

#pragma once

// ============================================================
// AudioSpeedController - 变速不变调（5.4）
//
// 需求：
//   1.5 倍速播放，但不改变音调（女声不变男声）
//
// 原理：SOLA（Synchronous Overlap Add，同步重叠相加）
//
//   把 PCM 切成窗口，按比例跳过/重复窗口，
//   相邻窗口做交叉淡化（crossfade）拼接：
//
//   输入窗口:  [===========]  [===========]
//                Ha(输入步长)
//
//   输出窗口:  [====][====]   重叠部分交叉淡化
//              Hs(输出步长)
//
//   输入/输出长度比 = Ha/Hs = speed
//   交叉淡化让跳变平滑，从而保持音调
//
// 数据流：
//   Resampler(S16) ----> AudioSpeedController ----> AudioDevice
//
// speed = 1.0 时完全直通（bypass），无延迟
// ============================================================

#include <cstdint>
#include <vector>
#include <atomic>

class AudioSpeedController
{
public:

    AudioSpeedController();

    ~AudioSpeedController();

    // 初始化
    // sampleRate: 采样率（例如 48000）
    // channels:   声道数（例如 2）
    bool Init(
        int sampleRate,
        int channels);

    // 设置播放速度（建议 0.25 ~ 4.0）
    // 兼容旧接口：等价于 SetUserSpeed
    void SetSpeed(
        double speed);

    double GetSpeed() const;

    // ---------- 9.0：用户倍速与追帧倍速分离（评审意见） ----------

    // 用户主动倍速（0.5 / 1.0 / 1.5 / 2.0，钳制 0.25 ~ 4.0）
    void SetUserSpeed(
        double speed);

    // 直播追帧倍速（LiveLatencyController 输出，钳制 1.0 ~ 2.0）
    void SetChaseSpeed(
        double speed);

    // 最终生效速度 = userSpeed × chaseSpeed
    // （内部 SOLA 按此速度变速）
    double GetEffectiveSpeed() const;

    // 处理输入 PCM
    // in:     输入 S16 PCM
    // inBytes:输入字节数
    // out:    输出缓冲区
    // outCap: 输出缓冲区容量（字节）
    // 返回本次写入 out 的字节数
    // 注意：返回的字节数可能小于本次输入对应的全部输出，
    //       需要循环调用 Flush() 取完剩余输出
    int Process(
        const uint8_t* in,
        int inBytes,
        uint8_t* out,
        int outCap);

    // 取走剩余输出数据，返回写入 out 的字节数，取完返回 0
    int Flush(
        uint8_t* out,
        int outCap);

    // 重置所有状态（Seek 时调用）
    void Reset();

    // 当前内部待输出字节数
    int PendingBytes() const;

private:

    // 处理一个完整窗口
    void ProcessWindow();

    // 内部 FIFO 输出到 out
    int Drain(
        uint8_t* out,
        int outCap);

    // 按 user × chase 重算最终生效速度并更新输入步长
    void ApplyEffectiveSpeed();

    // 参数
    int sampleRate = 48000;

    int channels = 2;

    // 播放速度（渲染线程 SetUserSpeed / SetChaseSpeed，
    // Demux 线程 Process 跨线程访问）
    std::atomic<double> speed{ 1.0 };          // 最终生效速度（user × chase）

    std::atomic<double> userSpeed{ 1.0 };      // 用户倍速

    std::atomic<double> chaseSpeed{ 1.0 };     // 追帧倍速（仅直播使用）

    // SOLA 参数（单位：帧，1帧 = channels 个采样）
    static constexpr int WINDOW = 2048;      // 窗口长度（~43ms @48k）

    static constexpr int HOP_OUT = 1024;     // 输出步长 Hs = WINDOW/2

    static constexpr int OVERLAP = WINDOW - HOP_OUT;   // 重叠长度

    // 输入步长 Ha = Hs * speed（原子，随速度变化）
    std::atomic<int> hopIn{ HOP_OUT };

    // 输入缓冲（帧为单位，交织存储）
    std::vector<int16_t> inputBuf;

    size_t readPos = 0;                      // 输入读取位置（帧）

    // 上一个窗口的末尾重叠部分（原始数据，用于交叉淡化）
    std::vector<int16_t> prevTail;

    bool havePrev = false;

    // 输出 FIFO
    std::vector<int16_t> outFifo;
};

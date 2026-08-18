#include "Network/BufferController.h"

#include <chrono>

#include "Utils/Logger.h"

// ============================================================
// BufferController - 网络播放缓冲状态机（v2 重写）
// ============================================================

namespace
{
    // 当前时刻（毫秒，steady clock，与 SDL tick 量纲一致）
    int64_t NowMs()
    {
        return static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
    }
}

BufferController::BufferController()
{
}

void BufferController::SetLive(
    bool live)
{
    this->live = live;

    if (!live)
    {
        // 点播：不参与网络缓冲状态机
        state = BufferState::Playing;
    }
    else if (!modeExplicit)
    {
        // 直播：默认稳定缓冲模式
        SetMode(true);
    }
}

bool BufferController::IsLive() const
{
    return live;
}

void BufferController::SetMode(
    bool stable)
{
    this->stable = stable;

    modeExplicit = true;

    ApplyModeWatermarks();
}

void BufferController::ApplyModeWatermarks()
{
    if (stable)
    {
        // 稳定缓冲模式：四级水位 + 去抖 + 断流超时
        lowWaterMs = 750;

        targetWaterMs = 1500;

        highWaterMs = 1950;

        maxBufferMs = 3000;

        debounceMs = 300;

        stallTimeoutMs = 5000;
    }
    else
    {
        // 低延迟模式：保持旧版行为（target ≈ 300ms，立即进出）
        lowWaterMs = 150;

        targetWaterMs = 300;

        highWaterMs = 400;

        maxBufferMs = 500;

        debounceMs = 0;

        stallTimeoutMs = 5000;
    }
}

void BufferController::SetTargetBufferMs(
    int ms)
{
    if (ms > 0)
    {
        targetWaterMs = ms;

        // 水位随目标自动缩放（与旧版一致）：低 = 50% 目标，高 = 130% 目标
        lowWaterMs = ms / 2;

        highWaterMs = static_cast<int>(ms * 1.3);

        if (highWaterMs <= lowWaterMs)
        {
            highWaterMs = lowWaterMs + 1;
        }

        maxBufferMs = ms * 2;

        if (maxBufferMs <= highWaterMs)
        {
            maxBufferMs = highWaterMs + 100;
        }
    }
}

void BufferController::SetDebounceMs(
    int ms)
{
    if (ms >= 0)
    {
        debounceMs = ms;
    }
}

void BufferController::SetStallTimeoutMs(
    int ms)
{
    if (ms > 0)
    {
        stallTimeoutMs = ms;
    }
}

void BufferController::Update(
    double bufferedMs)
{
    this->bufferedMs =
        bufferedMs < 0.0 ?
        0.0 :
        bufferedMs;

    // 点播：不参与网络缓冲状态机
    if (!live)
    {
        return;
    }

    const int64_t now = NowMs();

    const int64_t last = lastPacketTick.load();

    // ---------- 断流检测 ----------
    // 有过收包记录，且超过 stallTimeoutMs 没有新包
    if (stallTimeoutMs > 0 &&
        last > 0 &&
        now - last > stallTimeoutMs)
    {
        if (state == BufferState::Playing ||
            state == BufferState::Rebuffering)
        {
            state = BufferState::Stalled;

            Logger::Info()
                << "[Buffer] -> Stalled (no packet > "
                << stallTimeoutMs
                << "ms)"
                << std::endl;

            bufferingEvent = true;

            enterEvent = true;

            // v2 Metrics
            stallCount.fetch_add(1);

            bufferingCount.fetch_add(1);

            EnterBuffering();
        }

        // 断流中：保持 Stalled，不再做水位判断
        return;
    }

    switch (state)
    {
    case BufferState::Stopped:
        // 等待 Start()
        break;

    case BufferState::Prebuffering:
        // 攒水到高水位，一次性放行
        if (this->bufferedMs >= highWaterMs)
        {
            state = BufferState::Playing;

            Logger::Info()
                << "[Buffer] Prebuffering -> Playing (buf="
                << static_cast<int>(bufferedMs)
                << "ms >= high "
                << highWaterMs
                << "ms)"
                << std::endl;

            lowWaterSince.store(-1);

            exitEvent = true;

            // v2 Metrics
            LeaveBuffering();
        }
        break;

    case BufferState::Playing:
        // 跌破低水位：去抖后进入 REBUFFERING（锁存）
        if (this->bufferedMs < lowWaterMs)
        {
            int64_t since = lowWaterSince.load();

            if (since < 0)
            {
                lowWaterSince.store(now);
            }
            else if (now - since >= debounceMs)
            {
                state = BufferState::Rebuffering;

                Logger::Info()
                    << "[Buffer] Playing -> Rebuffering (buf="
                    << static_cast<int>(bufferedMs)
                    << "ms < low "
                    << lowWaterMs
                    << "ms for "
                    << debounceMs
                    << "ms)"
                    << std::endl;

                lowWaterSince.store(-1);

                bufferingEvent = true;

                enterEvent = true;

                // v2 Metrics
                underrunCount.fetch_add(1);

                bufferingCount.fetch_add(1);

                EnterBuffering();
            }
        }
        else
        {
            lowWaterSince.store(-1);
        }
        break;

    case BufferState::Rebuffering:
        // 锁存：只有攒到高水位才恢复，中途波动不动作
        if (this->bufferedMs >= highWaterMs)
        {
            state = BufferState::Playing;

            Logger::Info()
                << "[Buffer] Rebuffering -> Playing (buf="
                << static_cast<int>(bufferedMs)
                << "ms >= high "
                << highWaterMs
                << "ms)"
                << std::endl;

            lowWaterSince.store(-1);

            exitEvent = true;

            // v2 Metrics
            LeaveBuffering();
        }
        break;

    case BufferState::Stalled:
        // 恢复收包（lastPacketTick 被刷新）→ 预缓冲
        if (last > 0 &&
            now - last <= stallTimeoutMs)
        {
            state = BufferState::Prebuffering;

            Logger::Info()
                << "[Buffer] Stalled -> Prebuffering (packet resumed)"
                << std::endl;

            lowWaterSince.store(-1);
        }
        break;
    }
}

void BufferController::OnPacketReceived()
{
    lastPacketTick.store(NowMs());
}

void BufferController::Reset()
{
    state = BufferState::Stopped;

    lastPacketTick.store(0);

    lowWaterSince.store(-1);

    bufferingEvent = false;

    enterEvent = false;

    exitEvent = false;

    // v2 Metrics：新媒体会话清零
    stallCount.store(0);

    underrunCount.store(0);

    bufferingCount.store(0);

    bufferingDurationMs.store(0);

    bufferingStartTick.store(-1);
}

void BufferController::Start()
{
    state = BufferState::Prebuffering;

    Logger::Info()
        << "[Buffer] Start -> Prebuffering"
        << std::endl;

    lastPacketTick.store(NowMs());

    lowWaterSince.store(-1);

    // v2 Metrics：启动预缓冲计入缓冲时长
    EnterBuffering();
}

BufferState BufferController::GetState() const
{
    return state;
}

const char* BufferController::GetStateName() const
{
    switch (state)
    {
    case BufferState::Stopped:       return "STOPPED";
    case BufferState::Prebuffering:  return "PREBUFFERING";
    case BufferState::Playing:       return "PLAYING";
    case BufferState::Rebuffering:   return "REBUFFERING";
    case BufferState::Stalled:       return "STALLED";
    }

    return "UNKNOWN";
}

bool BufferController::IsConsumingBlocked() const
{
    return
        state == BufferState::Prebuffering ||
        state == BufferState::Rebuffering ||
        state == BufferState::Stalled;
}

double BufferController::GetBufferedMs() const
{
    return bufferedMs.load();
}

int BufferController::GetTargetMs() const
{
    return targetWaterMs;
}

int BufferController::GetLowWaterMs() const
{
    return lowWaterMs;
}

int BufferController::GetHighWaterMs() const
{
    return highWaterMs;
}

int BufferController::GetMaxBufferMs() const
{
    return maxBufferMs;
}

bool BufferController::NeedBuffer() const
{
    return IsConsumingBlocked();
}

bool BufferController::IsEnough() const
{
    return state == BufferState::Playing;
}

bool BufferController::ConsumeBufferingEvent()
{
    bool evt = bufferingEvent.load();

    bufferingEvent.store(false);

    return evt;
}

bool BufferController::ConsumeEnterBufferingEvent()
{
    bool evt = enterEvent.load();

    enterEvent.store(false);

    return evt;
}

bool BufferController::ConsumeExitBufferingEvent()
{
    bool evt = exitEvent.load();

    exitEvent.store(false);

    return evt;
}

// ---------- 统计 ----------

void BufferController::EnterBuffering()
{
    int64_t start = bufferingStartTick.load();

    if (start < 0)
    {
        bufferingStartTick.store(NowMs());
    }
}

void BufferController::LeaveBuffering()
{
    int64_t start = bufferingStartTick.load();

    if (start >= 0)
    {
        bufferingDurationMs.fetch_add(NowMs() - start);

        bufferingStartTick.store(-1);
    }
}

int BufferController::GetStallCount() const
{
    return stallCount.load();
}

int BufferController::GetUnderrunCount() const
{
    return underrunCount.load();
}

int BufferController::GetBufferingCount() const
{
    return bufferingCount.load();
}

int64_t BufferController::GetBufferingDurationMs() const
{
    return bufferingDurationMs.load();
}

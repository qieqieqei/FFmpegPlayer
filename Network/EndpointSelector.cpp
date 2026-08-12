// ============================================================
// EndpointSelector.cpp - 端点多源择优（9.0）
//
// 评分说明：
//   - 未知指标（0）按中性处理，避免"未探测 = 最优"的假象；
//     RTT 未知给 50 分（与 50ms 等价），丢包未知给 100 分
//     （无损假设，与旧实现一致）。
//   - 防抖：新最优与上次得分差 < hysteresis 时沿用上次。
// ============================================================

#include "Network/EndpointSelector.h"

#include <algorithm>

EndpointSelector::EndpointSelector()
{
}

void EndpointSelector::SetWeights(
    double wRtt,
    double wLoss,
    double wPriority)
{
    this->wRtt = wRtt;

    this->wLoss = wLoss;

    this->wPriority = wPriority;
}

void EndpointSelector::SetHysteresis(
    double score)
{
    hysteresis =
        score > 0.0 ?
        score :
        0.0;
}

double EndpointSelector::ScoreRtt(
    double rttMs)
{
    if (rttMs <= 0.0)
    {
        // 未探测：中性分（等价 50ms）
        return 50.0;
    }

    double score =
        100.0 - rttMs;

    return std::max(0.0, score);
}

double EndpointSelector::ScoreLoss(
    double lossPercent)
{
    if (lossPercent <= 0.0)
    {
        // 未知 / 无损：满分
        return 100.0;
    }

    double score =
        100.0 - lossPercent * 10.0;

    return std::max(0.0, score);
}

double EndpointSelector::ScorePriority(
    int priority)
{
    double score =
        100.0 - priority * 10.0;

    return std::max(0.0, score);
}

EndpointDecision EndpointSelector::Select(
    const std::vector<EndpointCandidate>& candidates)
{
    EndpointDecision best;

    if (candidates.empty())
    {
        best.reason = "无候选端点";

        return best;
    }

    // ---------- 逐项评分 ----------

    double bestScore = -1.0;

    int bestIndex = 0;

    double wSum =
        wRtt + wLoss + wPriority;

    if (wSum <= 0.0)
    {
        wSum = 1.0;
    }

    for (std::size_t i = 0; i < candidates.size(); ++i)
    {
        const EndpointCandidate& c =
            candidates[i];

        double score =
            (wRtt * ScoreRtt(c.rttMs) +
                wLoss * ScoreLoss(c.lossPercent) +
                wPriority * ScorePriority(c.priority)) /
            wSum;

        if (score > bestScore)
        {
            bestScore = score;

            bestIndex = static_cast<int>(i);
        }
    }

    const EndpointCandidate& chosen =
        candidates[static_cast<std::size_t>(bestIndex)];

    // ---------- 防抖：与上次得分差小于阈值则保持上次 ----------

    if (haveLast &&
        hysteresis > 0.0 &&
        last.address == chosen.address &&
        last.port == chosen.port)
    {
        // 同一端点：无需切换
        return last;
    }

    if (haveLast &&
        hysteresis > 0.0 &&
        std::abs(bestScore - last.score) < hysteresis)
    {
        // 得分接近：保持上次选择，避免切换抖动
        return last;
    }

    // ---------- 提交新选择 ----------

    best.address = chosen.address;

    best.port = chosen.port;

    best.score = bestScore;

    best.reason =
        "rtt=" +
        std::to_string(static_cast<int>(chosen.rttMs)) +
        "ms loss=" +
        std::to_string(chosen.lossPercent) +
        "% pri=" +
        std::to_string(chosen.priority) +
        " score=" +
        std::to_string(bestScore);

    last = best;

    haveLast = true;

    return best;
}

bool EndpointSelector::HasSelection() const
{
    return haveLast;
}

const EndpointDecision& EndpointSelector::GetSelection() const
{
    return last;
}

void EndpointSelector::Reset()
{
    last = EndpointDecision();

    haveLast = false;
}

#pragma once

// ============================================================
// EndpointSelector - 端点多源择优（9.0，评审意见）
//
// 职责：从多个候选媒体源（如多路镜像 / 多 CDN 节点）中选择
//       最优端点，用于断流重连 / 首连选路。
//
// 评分模型（0~100，越高越好，加权求和）：
//   - RTT 分     ：100 - rttMs（200ms 归零；未知 RTT 记中性 50）
//   - 丢包分     ：100 - lossPercent * 10（10% 归零）
//   - 优先级分   ：100 - priority * 10（priority 10 归零）
//   权重默认 40% / 30% / 30%（可配，合计应约 1.0）
//
// 防抖：当前最优与上次最优得分差小于 hysteresis 时保持上次选择，
//       避免网络指标抖动导致端点频繁切换（重连风暴）。
// ============================================================

#include <string>
#include <vector>

// 候选端点
struct EndpointCandidate
{
    std::string address;        // IP / 域名
    int port = 0;               // 端口
    double rttMs = 0.0;         // 探测 RTT（毫秒；0 = 未知）
    double lossPercent = 0.0;   // 探测丢包率（0~100；0 = 未知）
    int priority = 0;           // 优先级（越小越优先；0 = 最高）
};

// 选择结果
struct EndpointDecision
{
    std::string address;        // 选中地址
    int port = 0;               // 选中端口
    double score = 0.0;         // 综合得分（0~100）
    std::string reason;         // 选择原因（中文，日志用）
};

class EndpointSelector
{
public:

    EndpointSelector();

    // 设置权重（rtt / loss / priority，默认 0.4 / 0.3 / 0.3）
    void SetWeights(
        double wRtt,
        double wLoss,
        double wPriority);

    // 防抖阈值（得分差，默认 0 = 关闭）
    void SetHysteresis(
        double score);

    // 选择最优端点（空列表返回空决策）
    EndpointDecision Select(
        const std::vector<EndpointCandidate>& candidates);

    // 是否有上一次选择
    bool HasSelection() const;

    // 上一次选择
    const EndpointDecision& GetSelection() const;

    // 重置状态（清空上一次选择）
    void Reset();

private:

    // 单项评分
    static double ScoreRtt(
        double rttMs);

    static double ScoreLoss(
        double lossPercent);

    static double ScorePriority(
        int priority);

    double wRtt = 0.4;          // RTT 权重

    double wLoss = 0.3;         // 丢包权重

    double wPriority = 0.3;     // 优先级权重

    double hysteresis = 0.0;    // 防抖阈值

    EndpointDecision last;      // 上一次选择

    bool haveLast = false;      // 是否有上一次选择
};

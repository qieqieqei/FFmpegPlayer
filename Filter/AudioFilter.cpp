#include "Filter/AudioFilter.h"

// ============================================================
// AudioFilter - 音频滤镜
// ============================================================

AudioFilter::AudioFilter()
{
}

AudioFilter::~AudioFilter()
{
    Close();
}

bool AudioFilter::Init(
    const std::string& filterDesc,
    AVSampleFormat sampleFmt,
    int sampleRate,
    uint64_t channelLayout)
{
    return
        graph.InitAudio(
            filterDesc,
            sampleFmt,
            sampleRate,
            channelLayout);
}

bool AudioFilter::Process(
    AVFrame* in,
    AVFrame** out)
{
    return
        graph.ProcessFrame(
            in,
            out);
}

void AudioFilter::Close()
{
    graph.Close();
}

bool AudioFilter::IsReady() const
{
    return graph.IsReady();
}

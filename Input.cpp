#include "Input.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}
#include <iostream>

bool OpenInput(
    const char* filename,
    AVFormatContext*& fmt,
    int& videoIndex,
    int& audioIndex,
    AVCodecParameters*& codecpar)
{
    int ret = avformat_open_input(
        &fmt,
        filename,
        nullptr,
        nullptr);

    if (ret < 0)
    {
        char errbuf[256];

        av_strerror(ret, errbuf, sizeof(errbuf));

        std::cout
            << "打开失败: "
            << errbuf
            << std::endl;

        return false;
    }

    std::cout << "打开成功" << std::endl;

    ret = avformat_find_stream_info(
        fmt,
        nullptr);

    if (ret < 0)
    {
        std::cout
            << "获取流信息失败"
            << std::endl;

        avformat_close_input(&fmt);

        return false;
    }

    videoIndex = -1;      // 初始化视频索引

    audioIndex = -1;      // 初始化音频索引


    for (unsigned int i = 0;
        i < fmt->nb_streams;
        i++)
    {
        AVStream* stream =
            fmt->streams[i];


        if (stream->codecpar->codec_type
            == AVMEDIA_TYPE_VIDEO)
        {
            videoIndex = i;     // 保存视频流编号
        }


        if (stream->codecpar->codec_type
            == AVMEDIA_TYPE_AUDIO)
        {
            audioIndex = i;     // 保存音频流编号
        }
    }

    if (videoIndex == -1)
    {
        std::cout
            << "没有找到视频流"
            << std::endl;

        avformat_close_input(&fmt);

        return false;
    }
    //视频没有必须失败。 音频没有：播放器仍然可以播放。
    if (audioIndex == -1)
    {
        std::cout
            << "没有找到音频流"
            << std::endl;
    }


    codecpar = fmt->streams[videoIndex]->codecpar;

    std::cout
        << "视频流索引: "
        << videoIndex
        << std::endl;

    std::cout
        << "音频流索引:"
        << audioIndex
        << std::endl;

    std::cout
        << "流数量: "
        << fmt->nb_streams
        << std::endl;

    std::cout
        << "时长(秒): "
        << fmt->duration / AV_TIME_BASE
        << std::endl;

    std::cout
        << "宽度: "
        << codecpar->width
        << std::endl;

    std::cout
        << "高度: "
        << codecpar->height
        << std::endl;

    std::cout
        << "Codec ID: "
        << codecpar->codec_id
        << std::endl;

    std::cout
        << "比特率: "
        << codecpar->bit_rate
        << std::endl;

    return true;
}
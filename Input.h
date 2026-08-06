#pragma once
extern"C" {
     #include <libavformat/avformat.h>
}

bool OpenInput(
    const char* filename,
    AVFormatContext*& fmt,
    int& videoIndex,
    int& audioIndex,
    AVCodecParameters*& codecpar);
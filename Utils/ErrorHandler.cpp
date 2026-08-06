#include "Utils/ErrorHandler.h"
#include "Utils/Logger.h"

extern "C" {
#include <libavutil/error.h>
}

#include <SDL.h>

#include <iostream>

const char* ErrorHandler::TagName(ErrorTag tag)
{
    switch (tag)
    {
    case ErrorTag::FFmpeg:      return "[FFmpeg]";
    case ErrorTag::SDL:         return "[SDL]";
    case ErrorTag::Decoder:     return "[Decoder]";
    case ErrorTag::Audio:       return "[Audio]";
    case ErrorTag::Video:       return "[Video]";
    case ErrorTag::Sync:        return "[Sync]";
    case ErrorTag::Queue:       return "[Queue]";
    case ErrorTag::Screenshot:  return "[Screenshot]";
    case ErrorTag::Player:      return "[Player]";
    case ErrorTag::General:
    default:                    return "[General]";
    }
}

std::string ErrorHandler::FFmpegError(int ret)
{
    char errbuf[256] = { 0 };

    av_strerror(ret, errbuf, sizeof(errbuf));

    return std::string(errbuf);
}

std::string ErrorHandler::SDLError()
{
    const char* err = SDL_GetError();

    return err ? std::string(err) : std::string("Unknown SDL error");
}

void ErrorHandler::Log(
    ErrorTag tag,
    const std::string& message)
{
    Logger::Error()
        << TagName(tag)
        << " "
        << message
        << std::endl;
}

void ErrorHandler::LogFFmpeg(
    ErrorTag tag,
    const std::string& context,
    int ret)
{
    Logger::Error()
        << TagName(tag)
        << " "
        << context
        << " failed : "
        << FFmpegError(ret)
        << " ("
        << ret
        << ")"
        << std::endl;
}

void ErrorHandler::LogSDL(
    ErrorTag tag,
    const std::string& context)
{
    Logger::Error()
        << TagName(tag)
        << " "
        << context
        << " failed : "
        << SDLError()
        << std::endl;
}

bool ErrorHandler::Check(
    bool ok,
    ErrorTag tag,
    const std::string& context)
{
    if (!ok)
    {
        Log(tag, context + " failed");
    }

    return ok;
}

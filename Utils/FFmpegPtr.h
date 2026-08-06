#pragma once

// ============================================================
// FFmpegPtr - RAII 资源管理（5.9）
//
// 把 FFmpeg 的裸指针封装成智能指针，离开作用域自动释放：
//
//   AVFramePtr frame = av_frame_alloc();      // 自动 av_frame_free
//   AVPacketPtr pkt  = av_packet_alloc();     // 自动 av_packet_free
//   AVCodecContextPtr ctx = avcodec_alloc_context3(codec);
//
// 支持：
//   get()      取裸指针
//   operator-> 访问成员
//   operator*  解引用
//   operator bool 判空
//   release()  放弃所有权（返回裸指针）
//   reset()    替换/释放
//
// 禁止拷贝，允许移动。
// ============================================================

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <utility>

template <typename T, void (*Deleter)(T**)>
class FFmpegPtr
{
public:

    FFmpegPtr() = default;

    explicit FFmpegPtr(T* ptr)
        : m_ptr(ptr)
    {
    }

    ~FFmpegPtr()
    {
        reset();
    }

    // 禁止拷贝
    FFmpegPtr(const FFmpegPtr&) = delete;

    FFmpegPtr& operator=(const FFmpegPtr&) = delete;

    // 允许移动
    FFmpegPtr(FFmpegPtr&& other) noexcept
        : m_ptr(other.release())
    {
    }

    FFmpegPtr& operator=(FFmpegPtr&& other) noexcept
    {
        if (this != &other)
        {
            reset(other.release());
        }

        return *this;
    }

    T* get() const
    {
        return m_ptr;
    }

    T* operator->() const
    {
        return m_ptr;
    }

    T& operator*() const
    {
        return *m_ptr;
    }

    explicit operator bool() const
    {
        return m_ptr != nullptr;
    }

    // 释放所有权，返回裸指针
    T* release()
    {
        T* ptr = m_ptr;

        m_ptr = nullptr;

        return ptr;
    }

    // 替换指针（旧的自动释放）
    void reset(T* ptr = nullptr)
    {
        if (m_ptr)
        {
            Deleter(&m_ptr);
        }

        m_ptr = ptr;
    }

private:

    T* m_ptr = nullptr;
};

// ============================================================
// 常用别名
// ============================================================

using AVFramePtr = FFmpegPtr<AVFrame, av_frame_free>;

using AVPacketPtr = FFmpegPtr<AVPacket, av_packet_free>;

using AVCodecContextPtr = FFmpegPtr<AVCodecContext, avcodec_free_context>;

using AVFormatContextPtr = FFmpegPtr<AVFormatContext, avformat_close_input>;

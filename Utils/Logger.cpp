#include "Utils/Logger.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>
#include <mutex>

// ============================================================
// Logger 实现
//
// 内部状态：
//   - 级别（原子，跨线程读写）
//   - 日志文件流（互斥锁保护）
//   - 时间戳：[HH:MM:SS.mmm]
// ============================================================

namespace
{
    // 当前级别（默认 INFO）
    std::atomic<LogLevel> g_level{
        LogLevel::Info };

    // 日志文件流
    std::ofstream g_file;

    // 写锁（保护文件流 + 控制台输出）
    std::mutex g_mutex;

    // 级别标签
    const char* LevelTag(
        LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        default:              return "?????";
        }
    }

    // 当前时间戳 [HH:MM:SS.mmm]
    std::string TimeStamp()
    {
        using namespace std::chrono;

        const auto now =
            system_clock::now();

        const auto ms =
            duration_cast<milliseconds>(
                now.time_since_epoch()) %
            1000;

        const std::time_t t =
            system_clock::to_time_t(now);

        std::tm local{};

        localtime_s(&local, &t);

        char buf[32];

        std::snprintf(
            buf,
            sizeof(buf),
            "%02d:%02d:%02d.%03d",
            local.tm_hour,
            local.tm_min,
            local.tm_sec,
            static_cast<int>(ms.count()));

        return buf;
    }
}

// 查询某级别当前是否启用（LogStream 构造时调用）
bool LogLevelEnabled(
    LogLevel level)
{
    return level >= g_level.load();
}

// ---------- 控制 ----------

void Logger::Init(
    LogLevel level,
    const std::string& logFile)
{
    std::lock_guard<std::mutex> lock(
        g_mutex);

    g_level.store(level);

    if (!logFile.empty())
    {
        g_file.close();

        g_file.open(
            logFile,
            std::ios::out |
            std::ios::app);
    }
}

void Logger::SetLevel(
    LogLevel level)
{
    g_level.store(level);
}

LogLevel Logger::GetLevel()
{
    return g_level.load();
}

void Logger::Close()
{
    std::lock_guard<std::mutex> lock(
        g_mutex);

    if (g_file.is_open())
    {
        g_file.close();
    }
}

// ---------- 流式日志入口 ----------

LogStream Logger::Debug()
{
    return LogStream(
        LogLevel::Debug);
}

LogStream Logger::Info()
{
    return LogStream(
        LogLevel::Info);
}

LogStream Logger::Warn()
{
    return LogStream(
        LogLevel::Warn);
}

LogStream Logger::Error()
{
    return LogStream(
        LogLevel::Error);
}

// ---------- 写入 ----------

void Logger::Write(
    LogLevel level,
    const std::string& message)
{
    // 级别过滤（低于当前级别直接丢弃）
    if (level < g_level.load())
    {
        return;
    }

    // 组装：[时间戳] [级别] 消息
    std::string line =
        "[" + TimeStamp() + "] " +
        "[" + LevelTag(level) + "] " +
        message;

    // 消息本身以换行结尾时（std::endl 已写入 \n），不再重复追加
    if (line.empty() ||
        line.back() != '\n')
    {
        line += '\n';
    }

    std::lock_guard<std::mutex> lock(
        g_mutex);

    // 日志量小，显式 flush（避免重定向到文件时全缓冲滞留）
    std::cout << line << std::flush;

    if (g_file.is_open())
    {
        g_file << line << std::flush;
    }
}

// LogStream 析构：写入日志
LogStream::~LogStream()
{
    Logger::Write(
        m_level,
        m_stream.str());
}

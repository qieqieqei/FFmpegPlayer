#pragma once

// ============================================================
// Logger - 轻量日志系统
//
// 用法（流式，与 std::cout 同风格）：
//
//   Logger::Info() << "播放状态 : " << state;
//   Logger::Error() << "打开失败 : " << filename;
//
//   // std::endl 保留可用（仅换行，不 flush）
//   Logger::Debug() << "每帧信息" << std::endl;
//
// 特性：
//   - 级别过滤：DEBUG < INFO < WARN < ERROR
//   - 时间戳前缀：[HH:MM:SS.mmm] [级别] 消息
//   - 可选日志文件（Init 传入路径，追加写）
//   - 跨线程安全（内部互斥锁）
//
// 级别设定：
//   - 默认 INFO（仅输出 INFO/WARN/ERROR）
//   - 命令行 -v 开 DEBUG
//   - --log-file xxx.log 同时写文件
// ============================================================

#include <ostream>
#include <sstream>
#include <string>

// 日志级别
enum class LogLevel
{
    Debug = 0,
    Info  = 1,
    Warn  = 2,
    Error = 3
};

// 前向声明（LogStream 构造时查询当前级别）
class Logger;

// 查询某级别当前是否启用（定义在 Logger.cpp）
bool LogLevelEnabled(
    LogLevel level);

// 日志流代理：语句结束时（析构）把内容写入 Logger
class LogStream
{

public:

    LogStream(
        LogLevel level)
        : m_level(level)
        , m_enabled(
            LogLevelEnabled(level))
    {
    }

    ~LogStream();

    // 流式拼接（任意可 << 类型）
    template <typename T>
    LogStream& operator<<(
        const T& value)
    {
        // 级别被过滤时零开销（不拼接）
        if (m_enabled)
        {
            m_stream << value;
        }

        return *this;
    }

    // std::endl / std::flush 等操纵符（仅换行）
    LogStream& operator<<(
        std::ostream& (*manipulator)(
            std::ostream&))
    {
        (void)manipulator;

        if (m_enabled)
        {
            m_stream << '\n';
        }

        return *this;
    }

private:

    LogLevel m_level;

    bool m_enabled;

    std::ostringstream m_stream;

};

class Logger
{

public:

    // ---------- 控制 ----------

    // 初始化（级别 + 可选日志文件路径）
    static void Init(
        LogLevel level,
        const std::string& logFile = "");

    // 设置级别
    static void SetLevel(
        LogLevel level);

    // 当前级别
    static LogLevel GetLevel();

    // 关闭（关闭日志文件）
    static void Close();

    // ---------- 流式日志入口 ----------

    static LogStream Debug();

    static LogStream Info();

    static LogStream Warn();

    static LogStream Error();

    // 真正写入一条日志（LogStream 析构时调用）
    static void Write(
        LogLevel level,
        const std::string& message);

private:

    Logger() = delete;

};

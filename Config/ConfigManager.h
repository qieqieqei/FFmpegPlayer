#pragma once

// ============================================================
// ConfigManager - 配置系统（7.11）
//
// 读取 player.json / stream.json，避免代码写死：
//
//   ConfigManager config;
//   config.Load(".");                       // 查找 exe 目录 / 当前目录
//
//   const StreamConfig& sc = config.GetStreamConfig();
//   const PlayerConfig& pc = config.GetPlayerConfig();
//
// 行为：
//   - 文件不存在 -> 使用内置默认值（不报错）
//   - 字段缺失   -> 使用该字段默认值
//   - 文件损坏   -> 跳过该文件并告警
//
// 内置极简 JSON 解析器（ConfigManager.cpp 内部实现），
// 支持对象 / 数组 / 字符串 / 数字 / 布尔 / null，无需第三方依赖。
//
// 工具函数：
//   ConfigManager::IsNetworkUrl(url)    // rtsp:// rtmp:// http(s)://
//   ConfigManager::ProtocolOf(url)      // "rtsp" / "rtmp" / "http" / "file"
//   ConfigManager::StripFileScheme(url) // 去掉 file:// 前缀
// ============================================================

#include <string>

#include "Config/PlayerConfig.h"
#include "Config/StreamConfig.h"

class ConfigManager
{
public:

    ConfigManager();

    // 加载 directory 下的 player.json / stream.json
    // directory 为空时自动搜索：exe 所在目录 -> 当前工作目录
    bool Load(
        const std::string& directory = "");

    const PlayerConfig& GetPlayerConfig() const;

    const StreamConfig& GetStreamConfig() const;

    // ---------- URL 工具 ----------

    // 是否为网络流 URL（rtsp / rtmp / http / https）
    static bool IsNetworkUrl(
        const std::string& url);

    // 提取协议名："rtsp" / "rtmp" / "http" / "https" / "file" / ""
    static std::string ProtocolOf(
        const std::string& url);

    // 去掉 "file://" 前缀（本地路径）
    static std::string StripFileScheme(
        const std::string& url);

private:

    // 加载单个 JSON 文件到配置结构
    void LoadPlayer(
        const std::string& path);

    void LoadStream(
        const std::string& path);

    // 搜索配置文件所在目录（返回第一个存在的目录）
    static std::string FindConfigDir(
        const std::string& directory);

    // 文件是否存在
    static bool FileExists(
        const std::string& path);

    PlayerConfig player;     // player.json

    StreamConfig stream;     // stream.json
};

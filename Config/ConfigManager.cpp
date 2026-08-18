#include "Config/ConfigManager.h"

#include "Utils/Logger.h"

#include <fstream>
#include <sstream>
#include <map>
#include <vector>
#include <cstdlib>
#include <cctype>
#include <cstdio>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

// ============================================================
// 极简 JSON 解析器（内部实现，无第三方依赖）
//
// 支持：对象 / 数组 / 字符串（含转义与 \uXXXX）/ 数字 / 布尔 / null
// 用途：解析 player.json / stream.json 这类扁平配置文件
// ============================================================

namespace
{

struct JsonValue
{
    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Object,
        Array
    };

    Type type = Type::Null;

    bool b = false;

    double num = 0.0;

    std::string str;

    std::map<std::string, JsonValue> obj;

    std::vector<JsonValue> arr;
};

// Unicode 码点 -> UTF-8 追加到字符串
void AppendUtf8(
    std::string& out,
    unsigned int cp)
{
    if (cp <= 0x7F)
    {
        out += static_cast<char>(cp);
    }
    else if (cp <= 0x7FF)
    {
        out += static_cast<char>(0xC0 | (cp >> 6));

        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else if (cp <= 0xFFFF)
    {
        out += static_cast<char>(0xE0 | (cp >> 12));

        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));

        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else
    {
        out += static_cast<char>(0xF0 | (cp >> 18));

        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));

        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));

        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// 解析 "\uXXXX"，返回码点；失败返回 0xFFFFFFFF
unsigned int ParseHex4(
    const std::string& s,
    size_t pos)
{
    unsigned int v = 0;

    for (size_t i = 0; i < 4; i++)
    {
        char c = s[pos + i];

        v <<= 4;

        if (c >= '0' && c <= '9')
        {
            v |= static_cast<unsigned int>(c - '0');
        }
        else if (c >= 'a' && c <= 'f')
        {
            v |= static_cast<unsigned int>(c - 'a' + 10);
        }
        else if (c >= 'A' && c <= 'F')
        {
            v |= static_cast<unsigned int>(c - 'A' + 10);
        }
        else
        {
            return 0xFFFFFFFF;
        }
    }

    return v;
}

class JsonParser
{
public:

    static bool Parse(
        const std::string& text,
        JsonValue& out)
    {
        JsonParser parser(text);

        parser.SkipWs();

        if (!parser.ParseValue(out))
        {
            return false;
        }

        parser.SkipWs();

        // 解析完必须到结尾（防止只解析了前半段）
        return parser.pos == parser.s.size();
    }

private:

    explicit JsonParser(
        const std::string& text)
        : s(text)
    {
    }

    void SkipWs()
    {
        while (pos < s.size())
        {
            char c = s[pos];

            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            {
                pos++;
            }
            else
            {
                break;
            }
        }
    }

    bool Peek(
        char c) const
    {
        return pos < s.size() && s[pos] == c;
    }

    bool ParseValue(
        JsonValue& out)
    {
        if (pos >= s.size())
        {
            return false;
        }

        char c = s[pos];

        if (c == '{')
        {
            return ParseObject(out);
        }

        if (c == '[')
        {
            return ParseArray(out);
        }

        if (c == '"')
        {
            out.type = JsonValue::Type::String;

            return ParseString(out.str);
        }

        if (c == 't' || c == 'f' || c == 'n')
        {
            return ParseLiteral(out);
        }

        if (c == '-' || (c >= '0' && c <= '9'))
        {
            return ParseNumber(out);
        }

        return false;
    }

    bool ParseObject(
        JsonValue& out)
    {
        out.type = JsonValue::Type::Object;

        pos++;   // '{'

        SkipWs();

        if (Peek('}'))
        {
            pos++;

            return true;
        }

        while (pos < s.size())
        {
            SkipWs();

            if (!Peek('"'))
            {
                return false;
            }

            std::string key;

            if (!ParseString(key))
            {
                return false;
            }

            SkipWs();

            if (!Peek(':'))
            {
                return false;
            }

            pos++;   // ':'

            SkipWs();

            JsonValue value;

            if (!ParseValue(value))
            {
                return false;
            }

            out.obj[key] = std::move(value);

            SkipWs();

            if (Peek(','))
            {
                pos++;

                continue;
            }

            if (Peek('}'))
            {
                pos++;

                return true;
            }

            return false;
        }

        return false;
    }

    bool ParseArray(
        JsonValue& out)
    {
        out.type = JsonValue::Type::Array;

        pos++;   // '['

        SkipWs();

        if (Peek(']'))
        {
            pos++;

            return true;
        }

        while (pos < s.size())
        {
            SkipWs();

            JsonValue value;

            if (!ParseValue(value))
            {
                return false;
            }

            out.arr.push_back(std::move(value));

            SkipWs();

            if (Peek(','))
            {
                pos++;

                continue;
            }

            if (Peek(']'))
            {
                pos++;

                return true;
            }

            return false;
        }

        return false;
    }

    bool ParseString(
        std::string& out)
    {
        if (!Peek('"'))
        {
            return false;
        }

        pos++;   // '"'

        out.clear();

        while (pos < s.size())
        {
            char c = s[pos];

            if (c == '"')
            {
                pos++;

                return true;
            }

            if (c == '\\')
            {
                pos++;

                if (pos >= s.size())
                {
                    return false;
                }

                char e = s[pos];

                switch (e)
                {
                case '"':  out += '"';  break;

                case '\\': out += '\\'; break;

                case '/':  out += '/';  break;

                case 'b':  out += '\b'; break;

                case 'f':  out += '\f'; break;

                case 'n':  out += '\n'; break;

                case 'r':  out += '\r'; break;

                case 't':  out += '\t'; break;

                case 'u':
                {
                    if (pos + 4 >= s.size())
                    {
                        return false;
                    }

                    unsigned int cp =
                        ParseHex4(s, pos + 1);

                    if (cp == 0xFFFFFFFF)
                    {
                        return false;
                    }

                    pos += 4;

                    // 代理对：高代理 + \uXXXX 低代理
                    if (cp >= 0xD800 && cp <= 0xDBFF &&
                        pos + 6 < s.size() &&
                        s[pos + 1] == '\\' &&
                        s[pos + 2] == 'u')
                    {
                        unsigned int low =
                            ParseHex4(s, pos + 3);

                        if (low >= 0xDC00 && low <= 0xDFFF)
                        {
                            cp =
                                0x10000 +
                                ((cp - 0xD800) << 10) +
                                (low - 0xDC00);

                            pos += 6;
                        }
                    }

                    AppendUtf8(out, cp);

                    break;
                }

                default:
                    return false;
                }

                pos++;
            }
            else
            {
                out += c;

                pos++;
            }
        }

        return false;
    }

    bool ParseNumber(
        JsonValue& out)
    {
        size_t start = pos;

        if (Peek('-'))
        {
            pos++;
        }

        bool hasDigit = false;

        while (pos < s.size() &&
            s[pos] >= '0' && s[pos] <= '9')
        {
            pos++;

            hasDigit = true;
        }

        if (!hasDigit)
        {
            return false;
        }

        if (Peek('.'))
        {
            pos++;

            while (pos < s.size() &&
                s[pos] >= '0' && s[pos] <= '9')
            {
                pos++;
            }
        }

        if (Peek('e') || Peek('E'))
        {
            pos++;

            if (Peek('+') || Peek('-'))
            {
                pos++;
            }

            bool expDigit = false;

            while (pos < s.size() &&
                s[pos] >= '0' && s[pos] <= '9')
            {
                pos++;

                expDigit = true;
            }

            if (!expDigit)
            {
                return false;
            }
        }

        out.type = JsonValue::Type::Number;

        out.num =
            std::strtod(
                s.substr(start, pos - start).c_str(),
                nullptr);

        return true;
    }

    bool ParseLiteral(
        JsonValue& out)
    {
        if (s.compare(pos, 4, "true") == 0)
        {
            out.type = JsonValue::Type::Bool;

            out.b = true;

            pos += 4;

            return true;
        }

        if (s.compare(pos, 5, "false") == 0)
        {
            out.type = JsonValue::Type::Bool;

            out.b = false;

            pos += 5;

            return true;
        }

        if (s.compare(pos, 4, "null") == 0)
        {
            out.type = JsonValue::Type::Null;

            pos += 4;

            return true;
        }

        return false;
    }

    const std::string& s;

    size_t pos = 0;
};

// ---------- 取值工具 ----------

// 从对象中取键值（不存在或类型不符时返回默认值）
const JsonValue* Find(
    const JsonValue& root,
    const std::string& key)
{
    if (root.type != JsonValue::Type::Object)
    {
        return nullptr;
    }

    auto it = root.obj.find(key);

    if (it == root.obj.end())
    {
        return nullptr;
    }

    return &it->second;
}

std::string GetString(
    const JsonValue& root,
    const std::string& key,
    const std::string& def)
{
    const JsonValue* v = Find(root, key);

    if (v && v->type == JsonValue::Type::String)
    {
        return v->str;
    }

    return def;
}

double GetNumber(
    const JsonValue& root,
    const std::string& key,
    double def)
{
    const JsonValue* v = Find(root, key);

    if (v && v->type == JsonValue::Type::Number)
    {
        return v->num;
    }

    return def;
}

int GetInt(
    const JsonValue& root,
    const std::string& key,
    int def)
{
    return static_cast<int>(
        GetNumber(root, key, def));
}

bool GetBool(
    const JsonValue& root,
    const std::string& key,
    bool def)
{
    const JsonValue* v = Find(root, key);

    if (v && v->type == JsonValue::Type::Bool)
    {
        return v->b;
    }

    return def;
}

// 读取整个文件（二进制模式，保留 UTF-8 字节）
bool ReadFile(
    const std::string& path,
    std::string& out)
{
    std::ifstream file(
        path,
        std::ios::binary);

    if (!file)
    {
        return false;
    }

    std::ostringstream ss;

    ss << file.rdbuf();

    out = ss.str();

    return true;
}

}   // namespace

// ============================================================
// ConfigManager
// ============================================================

ConfigManager::ConfigManager()
{
}

bool ConfigManager::Load(
    const std::string& directory)
{
    // 优先 exe 所在目录，其次调用方指定目录 / 当前目录
    std::string dir =
        FindConfigDir(directory);

    if (dir.empty())
    {
        // 没有配置文件：全部使用默认值
        Logger::Info()
            << "[Config] No player.json / stream.json found, "
            << "using defaults"
            << std::endl;

        return true;
    }

    std::string playerPath =
        dir + "player.json";

    std::string streamPath =
        dir + "stream.json";

    if (FileExists(playerPath))
    {
        LoadPlayer(playerPath);
    }
    else
    {
        Logger::Info()
            << "[Config] player.json not found, using defaults"
            << std::endl;
    }

    if (FileExists(streamPath))
    {
        LoadStream(streamPath);
    }
    else
    {
        Logger::Info()
            << "[Config] stream.json not found, using defaults"
            << std::endl;
    }

    return true;
}

const PlayerConfig& ConfigManager::GetPlayerConfig() const
{
    return player;
}

const StreamConfig& ConfigManager::GetStreamConfig() const
{
    return stream;
}

bool ConfigManager::IsNetworkUrl(
    const std::string& url)
{
    std::string proto =
        ProtocolOf(url);

    return
        proto == "rtsp" ||
        proto == "rtmp" ||
        proto == "http" ||
        proto == "https";
}

std::string ConfigManager::ProtocolOf(
    const std::string& url)
{
    size_t pos =
        url.find("://");

    if (pos == std::string::npos)
    {
        return "";
    }

    std::string proto =
        url.substr(0, pos);

    // 统一小写
    for (auto& c : proto)
    {
        c = static_cast<char>(
            std::tolower(
                static_cast<unsigned char>(c)));
    }

    return proto;
}

std::string ConfigManager::StripFileScheme(
    const std::string& url)
{
    if (url.rfind("file://", 0) == 0)
    {
        return url.substr(7);
    }

    return url;
}

// ============================================================
// 内部实现
// ============================================================

void ConfigManager::LoadPlayer(
    const std::string& path)
{
    std::string text;

    if (!ReadFile(path, text))
    {
        return;
    }

    JsonValue root;

    if (!JsonParser::Parse(text, root))
    {
        Logger::Warn()
            << "[Config] player.json parse failed : "
            << path
            << std::endl;

        return;
    }

    player.windowWidth =
        GetInt(root, "window_width", player.windowWidth);

    player.windowHeight =
        GetInt(root, "window_height", player.windowHeight);

    player.volume =
        GetInt(root, "volume", player.volume);

    player.playbackSpeed =
        GetNumber(root, "playback_speed", player.playbackSpeed);

    player.fullscreen =
        GetBool(root, "fullscreen", player.fullscreen);

    player.defaultUrl =
        GetString(root, "default_url", player.defaultUrl);

    player.logDebug =
        GetBool(root, "log_debug", player.logDebug);

    player.logFile =
        GetString(root, "log_file", player.logFile);

    Logger::Info()
        << "[Config] player.json loaded : "
        << path
        << std::endl;
}

void ConfigManager::LoadStream(
    const std::string& path)
{
    std::string text;

    if (!ReadFile(path, text))
    {
        return;
    }

    JsonValue root;

    if (!JsonParser::Parse(text, root))
    {
        Logger::Warn()
            << "[Config] stream.json parse failed : "
            << path
            << std::endl;

        return;
    }

    stream.rtmpUrl =
        GetString(root, "rtmp_url", stream.rtmpUrl);

    stream.videoCodec =
        GetString(root, "video_codec", stream.videoCodec);

    stream.audioCodec =
        GetString(root, "audio_codec", stream.audioCodec);

    stream.bitrateKbps =
        GetInt(root, "bitrate_kbps", stream.bitrateKbps);

    stream.rtspTransport =
        GetString(root, "rtsp_transport", stream.rtspTransport);

    stream.rtspTimeoutMs =
        GetInt(root, "rtsp_timeout_ms", stream.rtspTimeoutMs);

    stream.networkTimeoutMs =
        GetInt(root, "network_timeout_ms", stream.networkTimeoutMs);

    stream.lowLatency =
        GetBool(root, "low_latency", stream.lowLatency);

    stream.reconnectMaxAttempts =
        GetInt(root, "reconnect_max_attempts", stream.reconnectMaxAttempts);

    stream.reconnectDelayMs =
        GetInt(root, "reconnect_delay_ms", stream.reconnectDelayMs);

    // 8.4：指数退避因子（默认 1.0 = 固定间隔）
    stream.reconnectBackoffFactor =
        GetNumber(root, "reconnect_backoff_factor", stream.reconnectBackoffFactor);

    stream.maxBufferPackets =
        GetInt(root, "max_buffer_packets", stream.maxBufferPackets);

    stream.bufferTargetMs =
        GetInt(root, "buffer_target_ms", stream.bufferTargetMs);

    // 8.5：直播队列最大时长 + 摄像头目标延迟
    stream.liveMaxQueueMs =
        GetInt(root, "live_max_queue_ms", stream.liveMaxQueueMs);

    // v2：直播缓冲模式（stable / low_latency），CLI --live-buffer 可覆盖
    stream.liveBufferMode =
        GetString(root, "live_buffer_mode", stream.liveBufferMode);

    stream.cameraLatencyMs =
        GetInt(root, "camera_latency_ms", stream.cameraLatencyMs);

    stream.hardwareDecode =
        GetBool(root, "hardware_decode", stream.hardwareDecode);

    stream.hlsSegmentDurationSec =
        GetInt(root, "hls_segment_duration_sec", stream.hlsSegmentDurationSec);

    stream.hlsListSize =
        GetInt(root, "hls_list_size", stream.hlsListSize);

    stream.videoFilter =
        GetString(root, "video_filter", stream.videoFilter);

    stream.audioFilter =
        GetString(root, "audio_filter", stream.audioFilter);

    Logger::Info()
        << "[Config] stream.json loaded : "
        << path
        << std::endl;
}

std::string ConfigManager::FindConfigDir(
    const std::string& directory)
{
    std::vector<std::string> candidates;

#ifdef _WIN32
    // exe 所在目录
    char exePath[MAX_PATH] = { 0 };

    if (GetModuleFileNameA(
        nullptr,
        exePath,
        MAX_PATH) > 0)
    {
        std::string exeDir(exePath);

        size_t slash =
            exeDir.find_last_of("\\/");

        if (slash != std::string::npos)
        {
            exeDir =
                exeDir.substr(0, slash + 1);

            candidates.push_back(exeDir);
        }
    }
#endif

    // 调用方指定目录 / 当前目录
    if (!directory.empty())
    {
        std::string dir = directory;

        if (dir.back() != '\\' &&
            dir.back() != '/')
        {
            dir += "\\";
        }

        candidates.push_back(dir);
    }

    // 第一个包含任一配置文件的目录
    for (const auto& dir : candidates)
    {
        if (FileExists(dir + "player.json") ||
            FileExists(dir + "stream.json"))
        {
            return dir;
        }
    }

    return "";
}

bool ConfigManager::FileExists(
    const std::string& path)
{
    std::ifstream file(path);

    return file.good();
}

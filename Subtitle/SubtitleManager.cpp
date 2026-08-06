#include "Subtitle/SubtitleManager.h"

#include "Utils/ErrorHandler.h"
#include "Utils/Logger.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iostream>

// ============================================================
// 解析实现
// ============================================================

SubtitleManager::SubtitleManager()
{
}

bool SubtitleManager::Load(
    const std::string& path)
{
    Clear();

    // ---------- 读取整个文件 ----------

    std::ifstream file(
        path,
        std::ios::binary);

    if (!file.is_open())
    {
        return false;
    }

    std::string content(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());

    // 去掉 UTF-8 BOM（EF BB BF）
    if (content.size() >= 3 &&
        static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF)
    {
        content.erase(0, 3);
    }

    // ---------- 按扩展名选择解析器 ----------

    std::string lower = path;

    std::transform(
        lower.begin(),
        lower.end(),
        lower.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(
                std::tolower(c));
        });

    bool ok = false;

    if (lower.size() >= 4 &&
        lower.substr(lower.size() - 4) == ".srt")
    {
        ok = ParseSrt(content);
    }
    else if (lower.size() >= 4 &&
        lower.substr(lower.size() - 4) == ".ass")
    {
        ok = ParseAss(content);
    }

    if (!ok)
    {
        Logger::Info()
            << "[Subtitle] Load failed : "
            << path
            << std::endl;

        return false;
    }

    // 按开始时间排序
    SortEntries(entries);

    loaded = true;

    enabled = true;

    Logger::Info()
        << "[Subtitle] Loaded : "
        << path
        << " ("
        << entries.size()
        << " entries)"
        << std::endl;

    return true;
}

std::string SubtitleManager::GetTextAt(
    double seconds) const
{
    if (!loaded ||
        !enabled ||
        entries.empty())
    {
        return "";
    }

    // 顺序查找（字幕条目通常不多，线性扫描足够）
    for (const Entry& entry : entries)
    {
        if (seconds >= entry.start &&
            seconds <= entry.end)
        {
            return entry.text;
        }
    }

    return "";
}

void SubtitleManager::Clear()
{
    entries.clear();

    loaded = false;
}

bool SubtitleManager::IsLoaded() const
{
    return loaded;
}

int SubtitleManager::Count() const
{
    return static_cast<int>(entries.size());
}

void SubtitleManager::SetEnabled(
    bool enabled)
{
    this->enabled = enabled;
}

bool SubtitleManager::IsEnabled() const
{
    return enabled;
}

// ============================================================
// SRT 解析
//
//  1
//  00:00:01,000 --> 00:00:03,500
//  你好，世界
//  (空行)
// ============================================================

bool SubtitleManager::ParseSrt(
    const std::string& content)
{
    std::istringstream stream(content);

    std::string line;

    while (std::getline(stream, line))
    {
        // 去掉行尾 \r
        if (!line.empty() &&
            line.back() == '\r')
        {
            line.pop_back();
        }

        // 序号行：跳过
        if (line.empty())
        {
            continue;
        }

        // 时间行：包含 "-->"
        size_t arrow =
            line.find("-->");

        if (arrow == std::string::npos)
        {
            // 不是时间行（可能是残留序号），继续找
            continue;
        }

        // 解析起止时间
        std::string startToken =
            line.substr(0, arrow);

        std::string endToken =
            line.substr(arrow + 3);

        double start = 0.0;

        double end = 0.0;

        // 去掉首尾空白
        auto trim =
            [](std::string& s)
            {
                size_t b = s.find_first_not_of(" \t");

                if (b == std::string::npos)
                {
                    s.clear();

                    return;
                }

                size_t e = s.find_last_not_of(" \t");

                s = s.substr(b, e - b + 1);
            };

        trim(startToken);

        trim(endToken);

        if (!ParseSrtTime(startToken, start) ||
            !ParseSrtTime(endToken, end))
        {
            continue;
        }

        // 收集文本行（直到空行）
        std::string text;

        while (std::getline(stream, line))
        {
            if (!line.empty() &&
                line.back() == '\r')
            {
                line.pop_back();
            }

            if (line.empty())
            {
                break;
            }

            if (!text.empty())
            {
                text += " ";
            }

            text += line;
        }

        if (text.empty())
        {
            continue;
        }

        Entry entry;

        entry.start = start;

        entry.end = end;

        entry.text = text;

        entries.push_back(entry);
    }

    return !entries.empty();
}

bool SubtitleManager::ParseSrtTime(
    const std::string& token,
    double& outSeconds)
{
    // 格式：HH:MM:SS,mmm
    int h = 0;

    int m = 0;

    int s = 0;

    int ms = 0;

    char comma = 0;

    std::istringstream iss(token);

    iss >> h;

    if (!iss || iss.get() != ':')
    {
        return false;
    }

    iss >> m;

    if (!iss || iss.get() != ':')
    {
        return false;
    }

    iss >> s;

    if (!iss || iss.get() != ',')
    {
        return false;
    }

    iss >> ms;

    if (!iss)
    {
        return false;
    }

    outSeconds =
        h * 3600.0 +
        m * 60.0 +
        s +
        ms / 1000.0;

    return true;
}

// ============================================================
// ASS 解析
//
//  [Events]
//  Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
//  Dialogue: 0,0:00:01.00,0:00:03.50,Default,,0,0,0,,你好{\\N}世界
// ============================================================

bool SubtitleManager::ParseAss(
    const std::string& content)
{
    std::istringstream stream(content);

    std::string line;

    // Format 行里各字段的顺序（默认从 Dialogue 行的第 10 个逗号后是文本）
    bool inEvents = false;

    int textFieldIndex = -1;   // Text 字段在 Format 里的位置

    while (std::getline(stream, line))
    {
        if (!line.empty() &&
            line.back() == '\r')
        {
            line.pop_back();
        }

        // 进入 [Events] 段
        if (line.find("[Events]") != std::string::npos)
        {
            inEvents = true;

            continue;
        }

        // 离开事件段
        if (!line.empty() &&
            line.front() == '[' &&
            inEvents)
        {
            inEvents = false;

            continue;
        }

        if (!inEvents)
        {
            continue;
        }

        // Format 行：记录字段顺序
        if (line.rfind("Format:", 0) == 0)
        {
            std::string fields =
                line.substr(7);

            std::vector<std::string> names;

            std::istringstream fs(fields);

            std::string field;

            while (std::getline(fs, field, ','))
            {
                auto trim =
                    [](std::string& s)
                    {
                        size_t b = s.find_first_not_of(" \t");

                        if (b == std::string::npos)
                        {
                            s.clear();

                            return;
                        }

                        size_t e = s.find_last_not_of(" \t");

                        s = s.substr(b, e - b + 1);
                    };

                trim(field);

                names.push_back(field);
            }

            // 找 Text 字段位置
            for (size_t i = 0; i < names.size(); i++)
            {
                if (names[i] == "Text")
                {
                    textFieldIndex = static_cast<int>(i);

                    break;
                }
            }

            continue;
        }

        // Dialogue 行
        if (line.rfind("Dialogue:", 0) != 0)
        {
            continue;
        }

        std::string body =
            line.substr(9);

        // 按逗号拆分
        std::vector<std::string> fields;

        std::istringstream ds(body);

        std::string field;

        while (std::getline(ds, field, ','))
        {
            fields.push_back(field);
        }

        // 至少要有 Start / End / Text
        if (fields.size() < 10)
        {
            continue;
        }

        // 时间字段：Format 里 Start 在第 1 位（0 基），End 第 2 位
        double start = 0.0;

        double end = 0.0;

        if (!ParseAssTime(fields[1], start) ||
            !ParseAssTime(fields[2], end))
        {
            continue;
        }

        // Text 字段位置（默认第 9 位）
        int textIndex =
            (textFieldIndex >= 0) ?
            textFieldIndex :
            9;

        if (textIndex >= static_cast<int>(fields.size()))
        {
            continue;
        }

        // 文本里可能还有逗号：把 Text 字段之后的所有内容拼回去
        std::string text;

        for (int i = textIndex; i < static_cast<int>(fields.size()); i++)
        {
            if (i > textIndex)
            {
                text += ",";
            }

            text += fields[i];
        }

        text = CleanAssText(text);

        if (text.empty())
        {
            continue;
        }

        Entry entry;

        entry.start = start;

        entry.end = end;

        entry.text = text;

        entries.push_back(entry);
    }

    return !entries.empty();
}

bool SubtitleManager::ParseAssTime(
    const std::string& token,
    double& outSeconds)
{
    // 格式：H:MM:SS.cc（cc 是百分秒）
    int h = 0;

    int m = 0;

    int s = 0;

    int cs = 0;

    char dot = 0;

    std::istringstream iss(token);

    iss >> h;

    if (!iss || iss.get() != ':')
    {
        return false;
    }

    iss >> m;

    if (!iss || iss.get() != ':')
    {
        return false;
    }

    iss >> s;

    if (!iss || iss.get() != '.')
    {
        return false;
    }

    iss >> cs;

    if (!iss)
    {
        return false;
    }

    outSeconds =
        h * 3600.0 +
        m * 60.0 +
        s +
        cs / 100.0;

    return true;
}

std::string SubtitleManager::CleanAssText(
    const std::string& text)
{
    std::string result;

    result.reserve(text.size());

    for (size_t i = 0; i < text.size(); i++)
    {
        char c = text[i];

        // 跳过 {\...} 样式标签
        if (c == '{')
        {
            while (i < text.size() &&
                text[i] != '}')
            {
                i++;
            }

            continue;
        }

        // \N / \n 换行 -> 空格
        if (c == '\\' &&
            i + 1 < text.size() &&
            (text[i + 1] == 'N' ||
             text[i + 1] == 'n'))
        {
            result += ' ';

            i++;

            continue;
        }

        result += c;
    }

    // 去掉首尾空白
    size_t b = result.find_first_not_of(" \t");

    if (b == std::string::npos)
    {
        return "";
    }

    size_t e = result.find_last_not_of(" \t");

    return result.substr(b, e - b + 1);
}

void SubtitleManager::SortEntries(
    std::vector<Entry>& entries)
{
    std::sort(
        entries.begin(),
        entries.end(),
        [](const Entry& a, const Entry& b)
        {
            return a.start < b.start;
        });
}

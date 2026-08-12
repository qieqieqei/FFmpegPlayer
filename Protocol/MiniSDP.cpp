// ============================================================
// MiniSDP.cpp - 极简 SDP 解析 / 生成（9.0）
//
// SDP 行格式（RFC 4566 子集）：
//   v=0
//   o=<username> <sess-id> <sess-version> IN IP4 <addr>
//   s=<会话名>
//   c=IN IP4 <addr>              （会话级，可被媒体级覆盖）
//   t=0 0
//   a=control:<url>              （RTSP 扩展）
//   m=<type> <port>[/<count>] <proto> <fmt>...
//   a=rtpmap:<fmt> <name>/<clock>[/<channels>]
//   a=fmtp:<fmt> <参数>
//   a=control:<trackID=...>
//   i=<标题>
// ============================================================

#include "Protocol/MiniSDP.h"

#include <cctype>
#include <sstream>
#include <vector>

// ---------- 工具 ----------

std::string MiniSDP::TrimCr(
    const std::string& line)
{
    std::string s = line;

    while (!s.empty() &&
        (s.back() == '\r' ||
            s.back() == '\n'))
    {
        s.pop_back();
    }

    return s;
}

static std::string TrimSpaces(
    const std::string& s)
{
    std::size_t begin = 0;

    std::size_t end = s.size();

    while (begin < end &&
        std::isspace(
            static_cast<unsigned char>(s[begin])))
    {
        ++begin;
    }

    while (end > begin &&
        std::isspace(
            static_cast<unsigned char>(s[end - 1])))
    {
        --end;
    }

    return s.substr(begin, end - begin);
}

static std::vector<std::string> Split(
    const std::string& s,
    char delim)
{
    std::vector<std::string> parts;

    std::string cur;

    for (char c : s)
    {
        if (c == delim)
        {
            parts.push_back(cur);

            cur.clear();
        }
        else
        {
            cur.push_back(c);
        }
    }

    parts.push_back(cur);

    return parts;
}

// ---------- rtpmap / fmtp ----------

bool MiniSDP::ParseRtpmap(
    const std::string& line,
    MiniSdpMedia& media)
{
    // "a=rtpmap:96 H264/90000" 或 "a=rtpmap:97 MPEG4-GENERIC/48000/2"
    std::string body =
        line.substr(9); // 去掉 "a=rtpmap:"

    std::size_t space =
        body.find(' ');

    if (space == std::string::npos)
    {
        return false;
    }

    std::string ptStr =
        body.substr(0, space);

    std::string enc =
        body.substr(space + 1);

    // 负载类型
    try
    {
        media.payloadType =
            std::stoi(ptStr);
    }
    catch (...)
    {
        return false;
    }

    // 编码名 / 时钟 / 声道
    std::vector<std::string> parts =
        Split(enc, '/');

    if (parts.empty())
    {
        return false;
    }

    media.encodingName =
        parts[0];

    if (parts.size() >= 2)
    {
        try
        {
            media.clockRate =
                std::stoi(parts[1]);
        }
        catch (...)
        {
            media.clockRate = 0;
        }
    }

    if (parts.size() >= 3)
    {
        try
        {
            media.channels =
                std::stoi(parts[2]);
        }
        catch (...)
        {
            media.channels = 0;
        }
    }

    return true;
}

bool MiniSDP::ParseFmtp(
    const std::string& line,
    MiniSdpMedia& media)
{
    // "a=fmtp:96 packetization-mode=1;profile-level-id=..."
    std::string body =
        line.substr(7); // 去掉 "a=fmtp:"

    std::size_t space =
        body.find(' ');

    if (space == std::string::npos)
    {
        return false;
    }

    // 校验负载类型一致（不一致时仍记录，便于容错）
    std::string ptStr =
        body.substr(0, space);

    try
    {
        int pt =
            std::stoi(ptStr);

        if (media.payloadType >= 0 &&
            pt != media.payloadType)
        {
            return false;
        }

        media.payloadType = pt;
    }
    catch (...)
    {
        return false;
    }

    media.fmtp =
        TrimSpaces(
            body.substr(space + 1));

    return true;
}

// ---------- 解析 ----------

bool MiniSDP::Parse(
    const std::string& text)
{
    session = MiniSdpSession();

    if (text.empty())
    {
        return false;
    }

    std::istringstream stream(text);

    std::string line;

    MiniSdpMedia current;

    bool haveMedia = false;

    while (std::getline(stream, line))
    {
        line = TrimCr(line);

        if (line.empty())
        {
            continue;
        }

        char kind =
            line.empty() ?
            '\0' :
            line[0];

        switch (kind)
        {
        case 'v':
            // 版本行：忽略
            break;

        case 'o':
            session.origin =
                line.substr(2);

            break;

        case 's':
            session.sessionName =
                line.substr(2);

            break;

        case 'c':
            // 会话级 / 媒体级地址（媒体级覆盖）
            {
                std::size_t pos =
                    line.find("IN IP4");

                if (pos != std::string::npos)
                {
                    std::string addr =
                        TrimSpaces(
                            line.substr(pos + 7));

                    if (haveMedia)
                    {
                        // 媒体级 c= 行：SDP 中地址属于媒体，
                        // 此处统一存会话地址（自定义路径单地址够用）
                        session.connectionAddress =
                            addr;
                    }
                    else
                    {
                        session.connectionAddress =
                            addr;
                    }
                }
            }
            break;

        case 't':
            // 时间行：忽略
            break;

        case 'm':
            // 提交上一个媒体
            if (haveMedia)
            {
                session.medias.push_back(current);
            }

            current = MiniSdpMedia();

            {
                // "m=video 5000 RTP/AVP 96"
                std::vector<std::string> parts =
                    Split(line.substr(2), ' ');

                if (parts.size() >= 1)
                {
                    current.type = parts[0];
                }

                if (parts.size() >= 2)
                {
                    // 端口可能带 "/count"（多播），取端口部分
                    std::string portStr =
                        parts[1];

                    std::size_t slash =
                        portStr.find('/');

                    if (slash != std::string::npos)
                    {
                        portStr =
                            portStr.substr(0, slash);
                    }

                    try
                    {
                        current.port =
                            std::stoi(portStr);
                    }
                    catch (...)
                    {
                        current.port = 0;
                    }
                }

                if (parts.size() >= 3)
                {
                    current.proto = parts[2];
                }

                if (parts.size() >= 4)
                {
                    try
                    {
                        current.payloadType =
                            std::stoi(parts[3]);
                    }
                    catch (...)
                    {
                        current.payloadType = -1;
                    }
                }
            }

            haveMedia = true;

            break;

        case 'a':
            {
                if (line.compare(0, 9, "a=rtpmap:") == 0)
                {
                    if (haveMedia)
                    {
                        ParseRtpmap(line, current);
                    }
                }
                else if (line.compare(0, 7, "a=fmtp:") == 0)
                {
                    if (haveMedia)
                    {
                        ParseFmtp(line, current);
                    }
                }
                else if (line.compare(0, 10, "a=control:") == 0)
                {
                    std::string control =
                        TrimSpaces(
                            line.substr(10));

                    if (haveMedia)
                    {
                        current.control = control;
                    }
                    else
                    {
                        session.sessionControl = control;
                    }
                }
            }
            break;

        case 'i':
            if (haveMedia)
            {
                current.mediaTitle =
                    line.substr(2);
            }
            break;

        default:
            // 其他行（b= / k= / z= / 属性扩展）：忽略
            break;
        }
    }

    // 收尾：提交最后一个媒体
    if (haveMedia)
    {
        session.medias.push_back(current);
    }

    return !session.medias.empty();
}

const MiniSdpMedia* MiniSDP::FindMedia(
    const std::string& type) const
{
    for (const MiniSdpMedia& m : session.medias)
    {
        if (m.type == type)
        {
            return &m;
        }
    }

    return nullptr;
}

// ---------- 生成 ----------

std::string MiniSDP::Build(
    const MiniSdpSession& session)
{
    std::ostringstream out;

    out << "v=0\r\n";

    out << "o=" <<
        (session.origin.empty() ?
            std::string("- 0 0 IN IP4 0.0.0.0") :
            session.origin) <<
        "\r\n";

    out << "s=" <<
        (session.sessionName.empty() ?
            std::string("MiniSDP") :
            session.sessionName) <<
        "\r\n";

    if (!session.connectionAddress.empty())
    {
        out << "c=IN IP4 " <<
            session.connectionAddress <<
            "\r\n";
    }

    out << "t=0 0\r\n";

    if (!session.sessionControl.empty())
    {
        out << "a=control:" <<
            session.sessionControl <<
            "\r\n";
    }

    for (const MiniSdpMedia& m : session.medias)
    {
        out << "m=" <<
            m.type << " " <<
            m.port << " " <<
            (m.proto.empty() ?
                std::string("RTP/AVP") :
                m.proto) << " " <<
            m.payloadType <<
            "\r\n";

        if (!m.mediaTitle.empty())
        {
            out << "i=" <<
                m.mediaTitle <<
                "\r\n";
        }

        if (m.payloadType >= 0 &&
            !m.encodingName.empty())
        {
            out << "a=rtpmap:" <<
                m.payloadType << " " <<
                m.encodingName << "/" <<
                m.clockRate;

            if (m.channels > 0)
            {
                out << "/" << m.channels;
            }

            out << "\r\n";
        }

        if (!m.fmtp.empty() &&
            m.payloadType >= 0)
        {
            out << "a=fmtp:" <<
                m.payloadType << " " <<
                m.fmtp <<
                "\r\n";
        }

        if (!m.control.empty())
        {
            out << "a=control:" <<
                m.control <<
                "\r\n";
        }
    }

    return out.str();
}

std::string MiniSDP::BuildMedia(
    const std::string& type,
    int payloadType,
    const std::string& encodingName,
    int clockRate,
    int channels,
    const std::string& fmtp,
    const std::string& control,
    int port,
    const std::string& address)
{
    MiniSdpMedia media;

    media.type = type;

    media.port = port;

    media.proto = "RTP/AVP";

    media.payloadType = payloadType;

    media.encodingName = encodingName;

    media.clockRate = clockRate;

    media.channels = channels;

    media.fmtp = fmtp;

    media.control = control;

    MiniSdpSession session;

    session.connectionAddress = address;

    session.medias.push_back(media);

    return Build(session);
}

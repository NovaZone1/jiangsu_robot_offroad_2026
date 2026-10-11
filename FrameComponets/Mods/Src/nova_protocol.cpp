#include "nova_protocol.hpp"
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    const char *const commands[] = {"hello",   "get",        "configure", "heartbeat",
                                    "set_pid", "set_target", "stop"};

    bool TokenChar(char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '-';
    }

    // Bounded fixed-decimal writer avoids floating printf and its large task stack usage.
    class Writer
    {
    public:
        explicit Writer(char *buffer) : buffer_(buffer)
        {
            buffer_[0] = 0;
        }

        void Text(const char *text)
        {
            while (*text && valid_)
            {
                if (used_ >= 383)
                {
                    valid_ = false;
                    break;
                }
                buffer_[used_++] = *text++;
            }
            buffer_[used_] = 0;
        }

        void Integer(uint64_t value)
        {
            char digits[21];
            uint8_t count = 0;
            do
            {
                digits[count++] = '0' + value % 10U;
                value /= 10U;
            } while (value);
            while (count)
            {
                char c[] = {digits[--count], 0};
                Text(c);
            }
        }

        void Number(double value)
        {
            if (!isfinite(value) || fabs(value) > 1000000000.0)
            {
                valid_ = false;
                return;
            }
            if (value < 0)
            {
                Text("-");
                value = -value;
            }
            const uint64_t scaled = (uint64_t)(value * 1000000.0 + 0.5);
            Integer(scaled / 1000000U);
            Text(".");
            uint32_t fraction = scaled % 1000000U;
            for (uint32_t divisor = 100000U; divisor; divisor /= 10U)
            {
                char c[] = {(char)('0' + fraction / divisor), 0};
                Text(c);
                fraction %= divisor;
            }
        }

        void String(const char *value)
        {
            Text("\"");
            // Public metadata is restricted to ASCII identifiers, so cannot inject JSON.
            if (!value)
            {
                valid_ = false;
                return;
            }
            while (*value)
            {
                if (!TokenChar(*value))
                {
                    valid_ = false;
                    return;
                }
                char c[] = {*value++, 0};
                Text(c);
            }
            Text("\"");
        }

        void Gains(const NovaProtocol::Gains &g)
        {
            Text("\"gains\":[");
            Number(g.kp);
            Text(",");
            Number(g.ki);
            Text(",");
            Number(g.kd);
            Text("]");
        }

        bool Valid() const
        {
            return valid_;
        }

    private:
        char *buffer_;
        uint16_t used_ = 0;
        bool valid_ = true;
    };

    struct Parser
    {
        explicit Parser(const char *text) : cursor(text)
        {
        }

        const char *cursor;

        void Space()
        {
            while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r')
            {
                ++cursor;
            }
        }

        bool Take(char c)
        {
            Space();
            if (*cursor != c)
            {
                return false;
            }
            ++cursor;
            return true;
        }

        bool End()
        {
            Space();
            return *cursor == 0;
        }

        bool String(char *dest, size_t capacity)
        {
            if (!Take('"'))
            {
                return false;
            }
            size_t size = 0;
            while (*cursor && *cursor != '"')
            {
                if (!TokenChar(*cursor) || size + 1 >= capacity)
                {
                    return false;
                }
                dest[size++] = *cursor++;
            }
            dest[size] = 0;
            return size && Take('"');
        }

        bool Number(float &value)
        {
            Space();
            const char *begin = cursor;
            if (*cursor == '-')
            {
                ++cursor;
            }
            if (*cursor == '0')
            {
                ++cursor;
            }
            else
            {
                if (*cursor < '1' || *cursor > '9')
                {
                    return false;
                }
                while (*cursor >= '0' && *cursor <= '9')
                {
                    ++cursor;
                }
            }
            if (*cursor == '.')
            {
                ++cursor;
                if (*cursor < '0' || *cursor > '9')
                {
                    return false;
                }
                while (*cursor >= '0' && *cursor <= '9')
                {
                    ++cursor;
                }
            }
            if (*cursor == 'e' || *cursor == 'E')
            {
                ++cursor;
                if (*cursor == '+' || *cursor == '-')
                {
                    ++cursor;
                }
                if (*cursor < '0' || *cursor > '9')
                {
                    return false;
                }
                while (*cursor >= '0' && *cursor <= '9')
                {
                    ++cursor;
                }
            }
            if (cursor - begin > 32)
            {
                return false;
            }
            char *end = nullptr;
            value = strtof(begin, &end);
            return end == cursor && isfinite(value) && fabsf(value) <= 1000000.0f;
        }

        bool Gains(NovaProtocol::Gains &g)
        {
            return Number(g.kp) && Take(',') && Number(g.ki) && Take(',') && Number(g.kd) &&
                   g.kp >= 0 && g.ki >= 0 && g.kd >= 0;
        }
    };

    bool ParseRequest(const char *text, char *id, NovaProtocol::Request &request)
    {
        Parser p(text);
        if (!p.Take('@') || !p.Take('{'))
        {
            return false;
        }
        uint16_t fields = 0;
        do
        {
            char key[24] = {}, value[24] = {};
            if (!p.String(key, sizeof(key)) || !p.Take(':'))
            {
                return false;
            }
            const char *const keys[] = {"type",         "id",      "cmd",        "channel",
                                        "gains",        "value",   "actual_min", "actual_max",
                                        "output_limit", "watchdog"};
            unsigned index = 0;
            while (index < 10 && strcmp(keys[index], key))
            {
                ++index;
            }
            if (index == 10 || (fields & (1U << index)))
            {
                return false;
            }
            fields |= 1U << index;
            float number = 0;
            switch (index)
            {
            case 0:
                if (!p.String(value, sizeof(value)) || strcmp(value, "command"))
                {
                    return false;
                }
                break;
            case 1:
                if (!p.String(id, 48))
                {
                    return false;
                }
                break;
            case 2:
            {
                if (!p.String(value, sizeof(value)))
                {
                    return false;
                }
                unsigned command = 0;
                while (command < 7 && strcmp(value, commands[command]))
                {
                    ++command;
                }
                if (command == 7)
                {
                    return false;
                }
                request.command = static_cast<NovaProtocol::Command>(command);
                break;
            }
            case 3:
                // JSON channel must be an integer, not a float that happens to round to one.
                p.Space();
                if (*p.cursor < '1' || *p.cursor > '9')
                {
                    return false;
                }
                while (*p.cursor >= '0' && *p.cursor <= '9')
                {
                    number = number * 10 + (*p.cursor++ - '0');
                    if (number > 255)
                    {
                        return false;
                    }
                }
                request.channel = (uint8_t)number;
                break;
            case 4:
                if (!p.Take('[') || !p.Gains(request.gains) || !p.Take(']'))
                {
                    return false;
                }
                break;
            default:
                if (!p.Number(number))
                {
                    return false;
                }
                if (index == 5)
                {
                    request.value = number;
                }
                if (index == 6)
                {
                    request.actual_min = number;
                }
                if (index == 7)
                {
                    request.actual_max = number;
                }
                if (index == 8)
                {
                    request.output_limit = number;
                }
                if (index == 9)
                {
                    request.watchdog = number;
                }
            }
        } while (p.Take(','));
        if (!p.Take('}') || !p.End())
        {
            return false;
        }
        uint16_t expected = 15;
        if (request.command == NovaProtocol::Command::SetPid)
        {
            expected |= 16;
        }
        if (request.command == NovaProtocol::Command::SetTarget)
        {
            expected |= 32;
        }
        if (request.command == NovaProtocol::Command::Configure)
        {
            expected |= 960;
        }
        // Nova emits heartbeat without type and intentionally reuses its heartbeat ID.
        return fields == expected ||
               (request.command == NovaProtocol::Command::Heartbeat && fields == (expected & ~1U));
    }
} // namespace

bool NovaProtocol::Init(const Callbacks &callbacks)
{
    if (!callbacks.send || !callbacks.execute)
    {
        return false;
    }
    callbacks_ = callbacks;
    stats_ = {};
    length_ = legacy_channel_ = next_cache_ = 0;
    dropping_ = false;
    for (auto &entry : cache_)
    {
        entry = {};
    }
    return true;
}

void NovaProtocol::DiscardUntilNewline()
{
    dropping_ = true;
    length_ = 0;
}

void NovaProtocol::Feed(const uint8_t *data, size_t size)
{
    if (!data || !callbacks_.execute)
    {
        return;
    }
    for (size_t i = 0; i < size; ++i)
    {
        const char byte = (char)data[i];
        if (byte == '\n')
        {
            if (!dropping_ && length_)
            {
                line_[length_] = 0;
                ProcessLine();
            }
            dropping_ = false;
            length_ = 0;
        }
        else if (!dropping_)
        {
            if (length_ + 1U >= sizeof(line_) || (byte < 32 && byte != '\r' && byte != '\t') ||
                (uint8_t)byte > 126)
            {
                ++stats_.invalid;
                DiscardUntilNewline();
            }
            else
            {
                line_[length_++] = byte;
            }
        }
    }
}

bool NovaProtocol::Send(const char *line)
{
    const bool ok = callbacks_.send &&
                    callbacks_.send(callbacks_.context, reinterpret_cast<const uint8_t *>(line),
                                    (uint16_t)strlen(line));
    if (!ok)
    {
        ++stats_.tx_dropped;
    }
    return ok;
}

void NovaProtocol::ProcessLine()
{
    ++stats_.received;
    if (line_[0] != '@')
    {
        ProcessLegacy();
        return;
    }
    Request request;
    char id[48] = {};
    if (!ParseRequest(line_, id, request))
    {
        ++stats_.invalid;
        Send("ERR:invalid_frame\r\n");
        return;
    }
    uint32_t hash = 2166136261U;
    for (const char *c = line_; *c; ++c)
    {
        hash = (hash ^ (uint8_t)*c) * 16777619U;
    }
    for (const auto &entry : cache_)
    {
        if (request.command == Command::Heartbeat || request.command == Command::Stop)
        {
            break; // Liveness and emergency stop must execute on every reception.
        }
        if (!strcmp(entry.id, id))
        {
            ++stats_.duplicates;
            if (hash == entry.hash)
            {
                Send(entry.response);
            }
            else
            {
                Send("ERR:id_reuse\r\n");
            }
            return;
        }
    }
    Reply reply;
    const bool ok = callbacks_.execute(callbacks_.context, request, reply);
    Writer w(output_);
    w.Text("@{\"type\":\"ack\",\"id\":");
    w.String(id);
    w.Text(",\"cmd\":");
    w.String(commands[static_cast<unsigned>(request.command)]);
    w.Text(ok ? ",\"ok\":true" : ",\"ok\":false");
    if (!ok)
    {
        w.Text(",\"error\":");
        w.String(reply.error);
    }
    else if (request.command == Command::Hello)
    {
        w.Text(",\"version\":2,\"capabilities\":[\"ack\",\"timestamp\"");
        const char *const caps[] = {"set_pid", "set_target", "stop", "watchdog", "limits"};
        for (uint8_t i = 0; i < 5; ++i)
        {
            if (reply.capabilities & (1U << i))
            {
                w.Text(",");
                w.String(caps[i]);
            }
        }
        w.Text("],\"algorithm\":");
        w.String(reply.algorithm);
        w.Text(",\"period\":");
        w.Number(reply.period);
    }
    else if (request.command == Command::Get || request.command == Command::SetPid)
    {
        w.Text(",");
        w.Gains(reply.gains);
        if (request.command == Command::Get)
        {
            w.Text(",\"target\":");
            w.Number(reply.target);
            w.Text(reply.enabled ? ",\"enabled\":true" : ",\"enabled\":false");
        }
    }
    else if (request.command == Command::SetTarget)
    {
        w.Text(",\"value\":");
        w.Number(reply.target);
    }
    w.Text("}\n");
    if (!w.Valid())
    {
        ++stats_.invalid;
        Send("ERR:response_range\r\n");
        return;
    }
    if (request.command != Command::Heartbeat && request.command != Command::Stop)
    {
        auto &entry = cache_[next_cache_];
        strcpy(entry.id, id);
        strcpy(entry.response, output_);
        entry.hash = hash;
        next_cache_ = (next_cache_ + 1U) % 4U;
    }
    Send(output_);
}

void NovaProtocol::ProcessLegacy()
{
    if (!strncmp(line_, "LOOP:", 5))
    {
        Parser p(line_ + 5);
        float channel;
        Request request;
        Reply reply;
        if (p.Number(channel) && p.End() && channel >= 1 && channel <= 255 &&
            channel == (uint8_t)channel)
        {
            request.channel = (uint8_t)channel;
            if (callbacks_.execute(callbacks_.context, request, reply))
            {
                legacy_channel_ = request.channel;
                Writer w(output_);
                w.Text("OK:LOOP:");
                w.Integer(legacy_channel_);
                w.Text("\r\n");
                Send(output_);
                return;
            }
        }
        Send("ERR:channel\r\n");
        return;
    }
    if (!legacy_channel_)
    {
        Send("ERR:select_LOOP_first\r\n");
        return;
    }
    Request request;
    request.channel = legacy_channel_;
    Reply reply;
    if (!callbacks_.execute(callbacks_.context, request, reply))
    {
        Send("ERR:not_ready\r\n");
        return;
    }
    if (!strcmp(line_, "PID?") || !strcmp(line_, "PID?\r"))
    {
        Writer w(output_);
        w.Text("PID:");
        w.Number(reply.gains.kp);
        w.Text(",");
        w.Number(reply.gains.ki);
        w.Text(",");
        w.Number(reply.gains.kd);
        w.Text("\r\n");
        if (w.Valid())
        {
            Send(output_);
        }
        return;
    }
    bool valid = !strncmp(line_, "PID:", 4);
    request.command = Command::SetPid;
    request.gains = reply.gains;
    Parser p(valid ? line_ + 4 : line_);
    if (valid && p.cursor[0] == 'K' &&
        (p.cursor[1] == 'p' || p.cursor[1] == 'i' || p.cursor[1] == 'd') && p.cursor[2] == '=')
    {
        float &gain = p.cursor[1] == 'p'
                          ? request.gains.kp
                          : (p.cursor[1] == 'i' ? request.gains.ki : request.gains.kd);
        p.cursor += 3;
        valid = p.Number(gain) && gain >= 0;
    }
    else if (valid)
    {
        valid = p.Gains(request.gains);
    }
    if (!valid || !p.End())
    {
        ++stats_.invalid;
        Send("ERR:invalid_PID\r\n");
        return;
    }
    if (callbacks_.execute(callbacks_.context, request, reply))
    {
        Send("OK:PID\r\n");
    }
    else
    {
        Writer w(output_);
        w.Text("ERR:");
        w.Text(reply.error);
        w.Text("\r\n");
        if (w.Valid())
        {
            Send(output_);
        }
    }
}

bool NovaProtocol::Telemetry(uint8_t channel, uint32_t sequence, double seconds, float setpoint,
                             float actual, float output)
{
    if (!channel || seconds < 0)
    {
        return false;
    }
    Writer w(output_);
    w.Text("@{\"type\":\"telemetry\",\"channel\":");
    w.Integer(channel);
    w.Text(",\"seq\":");
    w.Integer(sequence);
    w.Text(",\"time\":");
    w.Number(seconds);
    w.Text(",\"sp\":");
    w.Number(setpoint);
    w.Text(",\"pv\":");
    w.Number(actual);
    w.Text(",\"out\":");
    w.Number(output);
    w.Text("}\n");
    return w.Valid() && Send(output_);
}

bool NovaProtocol::Fault(uint8_t channel, const char *reason)
{
    Writer w(output_);
    w.Text("@{\"type\":\"fault\",\"channel\":");
    w.Integer(channel);
    w.Text(",\"reason\":");
    w.String(reason);
    w.Text("}\n");
    return w.Valid() && Send(output_);
}

const NovaProtocol::Stats &NovaProtocol::GetStats() const
{
    return stats_;
}

uint8_t NovaProtocol::GetLegacyChannel() const
{
    return legacy_channel_;
}

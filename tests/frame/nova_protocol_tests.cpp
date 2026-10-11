#include "nova_protocol.hpp"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string>
#include <vector>

struct Endpoint
{
    NovaProtocol protocol;
    NovaProtocol::Gains gains;
    std::vector<std::string> sent;
    unsigned applied = 0, stops = 0;
    bool busy = false, blocked_tx = false;

    Endpoint()
    {
        gains.kp = 0.0012f;
        gains.ki = 0.008f;
        NovaProtocol::Callbacks callbacks;
        callbacks.context = this;
        callbacks.send = [](void *context, const uint8_t *data, uint16_t size)
        {
            auto &self = *static_cast<Endpoint *>(context);
            if (self.blocked_tx)
            {
                return false;
            }
            self.sent.emplace_back(reinterpret_cast<const char *>(data), size);
            return true;
        };
        callbacks.execute =
            [](void *context, const NovaProtocol::Request &request, NovaProtocol::Reply &reply)
        {
            auto &self = *static_cast<Endpoint *>(context);
            if (request.channel > 5)
            {
                reply.error = "unknown_channel";
                return false;
            }
            if (request.command == NovaProtocol::Command::SetPid)
            {
                if (self.busy)
                {
                    reply.error = "busy";
                    return false;
                }
                ++self.applied;
                self.gains = request.gains;
            }
            if (request.command == NovaProtocol::Command::Stop)
            {
                ++self.stops;
            }
            reply.gains = self.gains;
            reply.period = 0.01f;
            reply.algorithm = "positional_pid_d_on_error";
            reply.capabilities = NovaProtocol::SetPid | NovaProtocol::Stop;
            return true;
        };
        assert(protocol.Init(callbacks));
    }

    void Feed(const std::string &line, size_t fragment = 1)
    {
        for (size_t i = 0; i < line.size(); i += fragment)
        {
            protocol.Feed(reinterpret_cast<const uint8_t *>(line.data() + i),
                          line.size() - i < fragment ? line.size() - i : fragment);
        }
    }
};

int main(int argc, char **argv)
{
    Endpoint e;
    if (argc == 2 && std::string(argv[1]) == "--interop")
    {
        e.Feed("@{\"type\":\"command\",\"id\":\"python-1\",\"cmd\":\"hello\",\"channel\":1}\n");
        e.Feed("@{\"type\":\"command\",\"id\":\"python-2\",\"cmd\":\"set_pid\",\"channel\":1,"
               "\"gains\":[0.001234,0.008,0]}\n");
        assert(e.protocol.Telemetry(1, 123, 2.46, 60, 59.25f, 0.23f));
        assert(e.protocol.Fault(5, "sample_unavailable"));
        for (const auto &line : e.sent)
        {
            printf("%s", line.c_str());
        }
        return 0;
    }
    e.Feed("PID:1,2,3\r\n");
    assert(e.applied == 0 && e.sent.back() == "ERR:select_LOOP_first\r\n");
    e.Feed("LOOP:4\r\nPID:0.01,0.02,0.003\r\nPID:Ki=0.03\r\n", 7);
    assert(e.protocol.GetLegacyChannel() == 4 && e.applied == 2);
    assert(e.gains.kp == .01f && e.gains.ki == .03f && e.gains.kd == .003f);
    e.Feed("PID?\r\n");
    assert(e.sent.back() == "PID:0.010000,0.030000,0.003000\r\n");
    const unsigned applied = e.applied;
    for (const auto bad :
         {"PID:1,2,3junk\n", "PID:NaN,1,0\n", "PID:1e999,1,0\n", "PID:-1,1,0\n", "PID:Kp=inf\n",
          "PID:Ki=\n", "PID:1,2\n", "PID:1,2,3,4\n", "PID:01,2,3\n"})
    {
        e.Feed(bad);
    }
    assert(e.applied == applied);
    e.Feed("PID:0.01,");
    e.protocol.DiscardUntilNewline();
    e.Feed("PID:1,2,3\nPID:Kd=0.004\n");
    assert(e.applied == applied + 1 && e.gains.kd == .004f);
    e.Feed(std::string(450, 'X') + "PID:1,2,3\nPID:Ki=0.04\n", 64);
    assert(e.applied == applied + 2);
    const std::string json = "@{\"type\":\"command\",\"id\":\"s-1\",\"cmd\":\"set_pid\","
                             "\"channel\":2,\"gains\":[0.002,0.01,0]}\n";
    e.Feed(json);
    assert(e.applied == applied + 3 && e.sent.back().find("\"ok\":true") != std::string::npos);
    const std::string ack = e.sent.back();
    e.Feed(json, 3);
    assert(e.applied == applied + 3 && e.sent.back() == ack &&
           e.protocol.GetStats().duplicates == 1);
    e.Feed("@{\"type\":\"command\",\"id\":\"s-1\",\"cmd\":\"stop\",\"channel\":2}\n");
    assert(e.stops == 1); // STOP is always executed, even if the sender reused an ID.
    for (const auto bad :
         {"@{\"type\":\"command\",\"id\":\"bad\",\"cmd\":\"stop\",\"channel\":1,\"channel\":2}\n",
          "@{\"type\":\"command\",\"id\":\"bad\",\"cmd\":\"stop\",\"channel\":1.0}\n",
          "@{\"type\":\"command\",\"id\":\"bad\",\"cmd\":\"stop\",\"channel\":0}\n",
          "@{\"type\":\"command\",\"id\":\"bad\",\"cmd\":\"stop\",\"channel\":true}\n",
          "@{\"type\":\"command\",\"id\":\"bad\",\"cmd\":\"set_pid\",\"channel\":1,\"gains\":[1,2,"
          "3,4]}\n",
          "@{\"type\":\"command\",\"id\":\"bad\",\"cmd\":\"set_pid\",\"channel\":1,\"gains\":[1,2,"
          "3]}trash\n"})
    {
        e.Feed(bad, 64);
        assert(e.sent.back() == "ERR:invalid_frame\r\n");
    }
    assert(e.stops == 1 && e.applied == applied + 3);
    e.busy = true;
    e.Feed("@{\"type\":\"command\",\"id\":\"s-2\",\"cmd\":\"set_pid\",\"channel\":2,\"gains\":[1,2,"
           "3]}\n");
    assert(e.sent.back().find("\"ok\":false,\"error\":\"busy\"") != std::string::npos);
    e.Feed("@{\"type\":\"command\",\"id\":\"s-3\",\"cmd\":\"stop\",\"channel\":2}\n");
    assert(e.stops == 2);
    e.blocked_tx = true;
    assert(!e.protocol.Telemetry(1, 42, 5.0, 100, 98, .6f));
    assert(e.protocol.GetStats().tx_dropped == 1);
    e.blocked_tx = false;
    assert(!e.protocol.Telemetry(1, 42, 5.0, NAN, 98, .6f));
    assert(!e.protocol.Fault(1, "bad\"string"));
    assert(e.protocol.Telemetry(5, 42, 5.0, 0, -.2f, 6));
    assert(e.sent.back().find("\"pv\":-0.200000") != std::string::npos);
    puts("PASS: Nova fragmented/coalesced frames, strict numbers/fields, loss resync, legacy "
         "selection, ACK replay, rejection and telemetry");
}

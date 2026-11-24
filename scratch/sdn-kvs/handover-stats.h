#ifndef SDN_KVS_HANDOVER_STATS_H
#define SDN_KVS_HANDOVER_STATS_H

#include "ns3/nstime.h"

#include <cstdint>
#include <map>
#include <optional>
#include <ostream>
#include <utility>
#include <vector>

namespace ns3
{

class HandoverStats
{
  public:
    static HandoverStats* Get();
    static void Enable();

    void Redirect(uint32_t server, Time now);
    void Restore(uint32_t server, Time now);
    void Enqueue(uint32_t server, uint32_t request, uint32_t keyset, Time now);
    void SubmitPacketOut(uint32_t server,
                         uint32_t request,
                         uint32_t keyset,
                         Time now,
                         bool accepted);
    void Egress(uint32_t server, uint32_t request, uint32_t keyset, Time now);
    void Arrive(uint32_t server, uint32_t request, uint32_t keyset, Time now);
    void Complete(uint32_t server, uint32_t request, uint32_t keyset, Time now);
    void Write(std::ostream& rounds, std::ostream& summary, uint32_t serverCount, Time end) const;

  private:
    using Key = std::pair<uint32_t, uint32_t>; // request ID, keyset ID, scoped to a server

    struct PacketTimes
    {
        Key key;
        Time enqueue;
        std::optional<Time> packetOut;
        std::optional<Time> send;
        std::optional<Time> arrive;
        Time idleAtArrival;
    };

    struct Round
    {
        uint32_t server;
        uint32_t id;
        Time redirect;
        std::optional<Time> restore;
        std::optional<Time> drain;
        Time idleAtDrain;
        size_t oldPending = 0;
        bool ambiguous = false;
        std::vector<PacketTimes> packets;
        std::optional<size_t> firstArrival;
    };

    struct Work
    {
        bool direct;
        std::vector<size_t> rounds;
    };

    struct ServerState
    {
        std::optional<size_t> active;
        std::optional<size_t> latest;
        uint32_t roundCount = 0;
        std::map<Key, Work> pending;
        std::map<Key, std::vector<size_t>> directInFlight;
        std::map<Key, std::pair<size_t, size_t>> redirected;
        std::optional<Time> lastDirectCompletion;
        Time idleAtLastDirectCompletion;
        Time idleSince;
        Time accumulatedIdle;

        Time IdleAt(Time now) const;
    };

    std::map<uint32_t, ServerState> m_servers;
    std::vector<Round> m_rounds;
};

} // namespace ns3

#endif

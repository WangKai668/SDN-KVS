#include "handover-stats.h"

#include <algorithm>
#include <iomanip>
#include <tuple>

namespace ns3
{

namespace
{
std::optional<HandoverStats> g_handoverStats;

double
Microseconds(Time time)
{
    return time.ToDouble(Time::US);
}

struct Summary
{
    std::vector<Time> idle;
    size_t excluded = 0;
    size_t unfinished = 0;
    size_t noWork = 0;
    size_t noForward = 0;
    size_t ambiguous = 0;

    void Write(std::ostream& out, const std::string& server)
    {
        std::sort(idle.begin(), idle.end());
        double total = 0;
        size_t nonzero = 0;
        for (Time time : idle)
        {
            total += Microseconds(time);
            nonzero += time.IsStrictlyPositive();
        }
        out << server << ',' << idle.size() << ',' << nonzero << ',';
        if (idle.empty())
        {
            out << "N/A,N/A,N/A,N/A";
        }
        else
        {
            // Nearest rank: ceil(0.95 * count), using one-based ranks.
            size_t rank = idle.size() - idle.size() / 20;
            out << double(nonzero) / idle.size() << ',' << total / idle.size() << ','
                << Microseconds(idle[rank - 1]) << ',' << Microseconds(idle.back());
        }
        out << ',' << excluded << ',' << unfinished << ',' << noWork << ',' << noForward << ','
            << ambiguous << '\n';
    }
};
} // namespace

HandoverStats*
HandoverStats::Get()
{
    return g_handoverStats ? &*g_handoverStats : nullptr;
}

void
HandoverStats::Enable()
{
    g_handoverStats.emplace();
}

Time
HandoverStats::ServerState::IdleAt(Time now) const
{
    return accumulatedIdle + (pending.empty() ? now - idleSince : Time());
}

void
HandoverStats::Redirect(uint32_t server, Time now)
{
    auto& state = m_servers[server];
    if (state.active)
    {
        return;
    }

    const size_t index = m_rounds.size();
    state.active = state.latest = index;
    m_rounds.emplace_back();
    auto& round = m_rounds.back();
    round.server = server;
    round.id = ++state.roundCount;
    round.redirect = now;

    // The server still owes these direct completions at the redirect command.
    for (auto& [key, work] : state.pending)
    {
        if (work.direct)
        {
            work.rounds.push_back(index);
            ++round.oldPending;
        }
    }
    for (auto& [key, rounds] : state.directInFlight)
    {
        rounds.push_back(index);
        ++round.oldPending;
    }
    if (round.oldPending == 0)
    {
        round.drain = state.lastDirectCompletion;
        round.idleAtDrain = state.idleAtLastDirectCompletion;
    }
}

void
HandoverStats::Restore(uint32_t server, Time now)
{
    auto& state = m_servers[server];
    if (state.active)
    {
        m_rounds[*state.active].restore = now;
        state.active.reset();
    }
}

void
HandoverStats::Enqueue(uint32_t server, uint32_t request, uint32_t keyset, Time now)
{
    auto& state = m_servers[server];
    if (!state.latest)
    {
        return;
    }

    Key key{request, keyset};
    auto& round = m_rounds[*state.latest];
    if (!state.active)
    {
        // A Packet-In can still be in flight after the restore command.
        round.ambiguous = true;
    }
    auto [position, inserted] =
        state.redirected.emplace(key, std::make_pair(*state.latest, round.packets.size()));
    if (!inserted)
    {
        round.ambiguous = true;
        m_rounds[position->second.first].ambiguous = true;
        return;
    }
    round.packets.emplace_back();
    round.packets.back().key = key;
    round.packets.back().enqueue = now;
}

void
HandoverStats::SubmitPacketOut(uint32_t server, uint32_t request, uint32_t keyset, Time now, bool accepted)
{
    auto& state = m_servers[server];
    auto match = state.redirected.find({request, keyset});
    if (match == state.redirected.end())
    {
        if (state.latest)
        {
            m_rounds[*state.latest].ambiguous = true;
        }
        return;
    }
    auto [roundIndex, packetIndex] = match->second;
    auto& round = m_rounds[roundIndex];
    auto& packet = round.packets[packetIndex];
    if (!accepted || packet.packetOut)
    {
        round.ambiguous = true;
        return;
    }
    packet.packetOut = now;
}

void
HandoverStats::Egress(uint32_t server, uint32_t request, uint32_t keyset, Time now)
{
    auto& state = m_servers[server];
    const Key key{request, keyset};
    auto match = state.redirected.find(key);
    if (match == state.redirected.end())
    {
        // This frame may reach the server after the restore command.
        auto [flight, inserted] = state.directInFlight.try_emplace(key);
        if (inserted && state.active)
        {
            auto& round = m_rounds[*state.active];
            flight->second.push_back(*state.active);
            ++round.oldPending;
            round.drain.reset();
        }
        return;
    }

    auto [roundIndex, packetIndex] = match->second;
    auto& round = m_rounds[roundIndex];
    auto& packet = round.packets[packetIndex];
    if (!packet.packetOut || now < *packet.packetOut || packet.send)
    {
        round.ambiguous = true;
        return;
    }
    packet.send = now;
}

void
HandoverStats::Arrive(uint32_t server, uint32_t request, uint32_t keyset, Time now)
{
    auto& state = m_servers[server];
    const Key key{request, keyset};
    auto match = state.redirected.find(key);
    const bool direct = match == state.redirected.end();
    const Time idle = state.IdleAt(now);
    auto [work, inserted] = state.pending.emplace(key, Work{direct, {}});
    if (!inserted)
    {
        if (state.latest)
        {
            m_rounds[*state.latest].ambiguous = true;
        }
        return;
    }
    state.accumulatedIdle = idle;

    auto flight = state.directInFlight.find(key);
    if (flight != state.directInFlight.end())
    {
        work->second.rounds = std::move(flight->second);
        state.directInFlight.erase(flight);
    }
    if (direct && state.active &&
        std::find(work->second.rounds.begin(), work->second.rounds.end(), *state.active) ==
            work->second.rounds.end())
    {
        auto& round = m_rounds[*state.active];
        work->second.rounds.push_back(*state.active);
        ++round.oldPending;
        round.drain.reset();
    }
    if (!direct)
    {
        auto [roundIndex, packetIndex] = match->second;
        auto& round = m_rounds[roundIndex];
        auto& packet = round.packets[packetIndex];
        if (!packet.send || now < *packet.send || packet.arrive)
        {
            round.ambiguous = true;
            return;
        }
        packet.arrive = now;
        packet.idleAtArrival = idle;
        if (!round.firstArrival)
        {
            round.firstArrival = packetIndex;
        }
    }
}

void
HandoverStats::Complete(uint32_t server, uint32_t request, uint32_t keyset, Time now)
{
    auto& state = m_servers[server];
    const Key key{request, keyset};
    auto work = state.pending.find(key);
    if (work == state.pending.end())
    {
        if (state.latest)
        {
            m_rounds[*state.latest].ambiguous = true;
        }
        return;
    }

    if (work->second.direct)
    {
        state.lastDirectCompletion = now;
        state.idleAtLastDirectCompletion = state.accumulatedIdle;
    }
    for (size_t index : work->second.rounds)
    {
        auto& round = m_rounds[index];
        if (--round.oldPending == 0)
        {
            round.drain = now;
            round.idleAtDrain = state.accumulatedIdle;
        }
    }
    state.pending.erase(work);
    state.redirected.erase(key);
    if (state.pending.empty())
    {
        state.idleSince = now;
    }
}

void
HandoverStats::Write(std::ostream& rounds,
                     std::ostream& summary,
                     uint32_t serverCount,
                     Time end) const
{
    auto time = [&rounds](std::optional<Time> value) {
        if (value)
        {
            rounds << Microseconds(*value);
        }
        rounds << ',';
    };
    rounds << std::fixed << std::setprecision(6)
           << "server_id,cycle_id,request_id,keyset_id,t_enqueue_us,t_send_us,t_drain_us,"
              "t_arrive_us,handover_idle_us,status,t_redirect_cmd_us,t_restore_cmd_us,"
              "t_packet_out_submit_us,simulation_end_us\n";
    summary << std::fixed << std::setprecision(6)
            << "server_id,valid,nonzero,nonzero_fraction,mean_idle_us,p95_idle_us,max_idle_us,"
               "excluded,unfinished,no_work,no_forward,unassociated\n";
    std::vector<Summary> totals(serverCount);
    Summary all;

    for (const auto& round : m_rounds)
    {
        // Select the first actual arrival even if a different packet was sent first.
        const PacketTimes* packet = nullptr;
        if (round.firstArrival)
        {
            packet = &round.packets[*round.firstArrival];
        }
        else if (!round.packets.empty())
        {
            auto first = std::min_element(
                round.packets.begin(), round.packets.end(), [](const auto& a, const auto& b) {
                    return a.send.value_or(Time::Max()) < b.send.value_or(Time::Max());
                });
            packet = &*first;
        }

        auto& stats = totals.at(round.server);
        const char* status;
        std::optional<Time> idle;
        if (round.ambiguous)
        {
            status = "unassociated";
            ++stats.ambiguous;
        }
        else if (!packet)
        {
            status = "no_forward";
            ++stats.noForward;
        }
        else if (!round.restore || !packet->arrive || round.oldPending > 0)
        {
            status = "unfinished";
            ++stats.unfinished;
        }
        else if (!round.drain || !packet->send || *packet->send < packet->enqueue)
        {
            status = "unassociated";
            ++stats.ambiguous;
        }
        else if (packet->enqueue > *round.drain)
        {
            const bool earlierWork =
                std::any_of(round.packets.begin(), round.packets.end(), [&round](const auto& other) {
                    return other.enqueue <= *round.drain;
                });
            // Another packet was available, but the first arrival was not that packet.
            status = earlierWork ? "unassociated" : "no_work_at_drain";
            if (earlierWork)
            {
                ++stats.ambiguous;
            }
            else
            {
                ++stats.noWork;
            }
        }
        else
        {
            status = "valid";
            idle = *packet->arrive <= *round.drain ? Time()
                                                   : packet->idleAtArrival - round.idleAtDrain;
            stats.idle.push_back(*idle);
            all.idle.push_back(*idle);
        }
        if (!idle)
        {
            ++stats.excluded;
        }

        rounds << round.server << ',' << round.id << ',';
        if (packet)
        {
            rounds << packet->key.first << ',' << packet->key.second << ',';
        }
        else
        {
            rounds << ",,";
        }
        time(packet ? std::optional<Time>(packet->enqueue) : std::nullopt);
        time(packet ? packet->send : std::nullopt);
        time(round.oldPending == 0 ? round.drain : std::nullopt);
        time(packet ? packet->arrive : std::nullopt);
        time(idle);
        rounds << status << ',';
        time(round.redirect);
        time(round.restore);
        time(packet ? packet->packetOut : std::nullopt);
        rounds << Microseconds(end) << '\n';
    }
    for (uint32_t server = 0; server < serverCount; ++server)
    {
        auto& stats = totals[server];
        all.excluded += stats.excluded;
        all.unfinished += stats.unfinished;
        all.noWork += stats.noWork;
        all.noForward += stats.noForward;
        all.ambiguous += stats.ambiguous;
        stats.Write(summary, std::to_string(server));
    }
    all.Write(summary, "all");
}

} // namespace ns3

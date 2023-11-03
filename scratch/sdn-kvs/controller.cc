#include "controller.h"

#include <ns3/core-module.h>
#include <ns3/internet-module.h>
#include <ns3/network-module.h>

#include <sstream>

NS_LOG_COMPONENT_DEFINE("Controller");
NS_OBJECT_ENSURE_REGISTERED(Controller);

Controller::Controller()
{
}

Controller::~Controller()
{
}

void
Controller::DoDispose()
{
    OFSwitch13Controller::DoDispose();
}

TypeId
Controller::GetTypeId()
{
    static TypeId tid = TypeId("ns3::Controller")
                            .SetParent<OFSwitch13Controller>()
                            .SetGroupName("SDN-KVS")
                            .AddConstructor<Controller>()
                            .AddAttribute("ServerNodeMap",
                                          "Map<server id, server node>. Storing all server nodes.",
                                          ObjectMapValue(),
                                          MakeObjectMapAccessor(&Controller::m_serverNodes),
                                          MakeObjectMapChecker<Node>())
                            .AddAttribute("ClientNodeMap",
                                          "Map<client id, client node>. Storing all client nodes.",
                                          ObjectMapValue(),
                                          MakeObjectMapAccessor(&Controller::m_clientNodes),
                                          MakeObjectMapChecker<Node>());
    return tid;
}

void
Controller::HandshakeSuccessful(Ptr<const RemoteSwitch> swtch)
{
    NS_LOG_INFO("[Controller][" << Simulator::Now().As(Time::S) << "] Handshake successfully");
    // Initialize the schedule context
    uint32_t server_num = m_serverNodes.size();
    for (uint32_t i = 0; i < server_num; i++)
    {
        ScheduleContext context;
        context.lastScheduleTime = Simulator::Now();
        context.queue_size = 0;
        context.server_rate = 0;
        context.schedulePackets = std::vector<SchedulePacket>();
        context.undoFlowEvents[1] = EventId();
        context.undoFlowEvents[2] = EventId();
        m_scheduleContexts[i] = context;
    }
    ConfigureSwitch(swtch);
}

void
Controller::AddToServerNodeMap(uint32_t server_id, Ptr<Node> server_node)
{
    m_serverNodes.insert(std::pair<uint32_t, Ptr<Node>>(server_id, server_node));
}

void
Controller::AddToClientNodeMap(uint32_t client_id, Ptr<Node> client_node)
{
    m_clientNodes.insert(std::pair<uint32_t, Ptr<Node>>(client_id, client_node));
}

ofl_err
Controller::HandlePacketIn(struct ofl_msg_packet_in* msg,
                           Ptr<const RemoteSwitch> swtch,
                           uint32_t xid)
{
    enum ofp_packet_in_reason reason = msg->reason;

    char* msgStr = ofl_structs_match_to_string((struct ofl_match_header*)msg->match, nullptr);
    NS_LOG_INFO("[Controller][" << Simulator::Now().As(Time::S) << "] Received Packet-In packet: "
                                << msgStr << " from switch " << swtch->GetDpId());
    free(msgStr);

    if (reason == OFPR_ACTION)
    {
        Ptr<Packet> packet = Create<Packet>(msg->data, msg->data_length); // Copy buffer to packet
        EthernetHeader ethHeader;
        Ipv4Header ipHeader;
        UdpHeader udpHeader;
        SdnKvsHeader sdnKvsHeader;
        // Parse header
        packet->RemoveHeader(ethHeader);
        packet->RemoveHeader(ipHeader);
        packet->RemoveHeader(udpHeader);
        packet->RemoveHeader(sdnKvsHeader);

        // Get source IP address
        Ipv4Address srcIp = ipHeader.GetSource();
        std::stringstream ss;
        ss << srcIp;
        // If the source IP comes from the client, the controller will schedule.
        // Else do flow table modification.
        // Note: Remember to free the message when everything is ok
        if (m_ipToClient.count(ss.str()))
            DoSchedule(sdnKvsHeader, msg, swtch, xid);
        else if (m_ipToServer.count(ss.str()))
            DoFlowModify(sdnKvsHeader, msg, swtch, xid);
    }
    else
    {
        NS_LOG_WARN("This controller can't handle the packet. Unexpected Packet-In reason.");
        // All handlers must free the message when everything is ok
        ofl_msg_free((struct ofl_msg_header*)msg, nullptr);
    }

    return 0;
}

void
Controller::ConfigureSwitch(Ptr<const RemoteSwitch> swtch)
{
    // Get the switch datapath ID
    uint64_t swDpId = swtch->GetDpId();

    // Broadcast ARP packet. It is important to add this rule for ARP protocol work well.
    DpctlExecute(swDpId, "flow-mod cmd=add,table=0,prio=100 eth_type=0x806 apply:output=all");
    DpctlExecute(swDpId, "flow-mod cmd=add,table=0,prio=100 eth_type=0x806 apply:output=all");

    // Let's assert the controller know the connection port between switch and client/server node.
    // Currently, the nodes are numbered sequentially starting from the client,
    // with even-numbered nodes connected to switch 1
    // and odd-numbered nodes connected to switch 2.
    // Make switch do routing at L3 network layer. So the match rule should parse the IP packet
    // header. No match rule packet will be dropped.

    // Think about 3 clients(id 0, 1, 2) and 3 servers(id 0, 1, 2),
    // there global id are 0(client), 1(client), 2(client), 3(server), 4(server), 5(server).
    // Switch 1 port 1 is connected to client 0(global id 0)
    // Switch 1 port 2 is connected to client 2(global id 2)
    // Switch 1 port 3 is connected to server 1(global id 4)
    // Switch 2 port 1 is connected to client 1(global id 1)
    // Switch 2 port 2 is connected to server 0(global id 3)
    // Switch 2 port 3 is connected to server 2(global id 5)
    // Swicth 1/2 port 4 is connected to the other switch.

    uint32_t client_num = m_clientNodes.size();
    uint32_t server_num = m_serverNodes.size();
    uint32_t port_cnt = 0; // Count the number of ports on the switch

    for (uint32_t i = 0; i < client_num; i++)
    {
        std::stringstream ss;
        std::string ip_dst = GetIpv4AddressFromNode(m_clientNodes[i]);
        m_ipToClient[ip_dst] = i; // Map IPv4 to client id

        int32_t port = GetSwitchPortByClientId(swDpId, i);
        if (port == -1) // Client node is not connected to this switch
            continue;
        ++port_cnt;
        ss.str(""); // Clear string stream
        ss << "flow-mod cmd=add,table=0,prio=100 "
           << "eth_type=0x0800,ip_dst=" << ip_dst << " apply:nw_dec,output=" << port;
        DpctlExecute(swDpId, ss.str());
    }
    for (uint32_t i = 0; i < server_num; i++)
    {
        std::stringstream ss;
        std::string ip_dst = GetIpv4AddressFromNode(m_serverNodes[i]);
        m_ipToServer[ip_dst] = i; // Map IPv4 to server id

        {
            // Set a flow entry with a higher priority so that
            // all data packets sent from the server node are sent to the controller.
            ss << "flow-mod cmd=add,table=0,prio=200 "
               << "eth_type=0x0800,ip_src=" << ip_dst << " apply:output=ctrl:128";
            DpctlExecute(swDpId, ss.str());
        }

        int32_t port = GetSwitchPortByServerId(swDpId, i);
        if (port == -1) // Server node is not connected to this switch
            continue;
        ++port_cnt;
        ss.str(""); // Clear string stream
        ss << "flow-mod cmd=add,table=0,prio=100 "
           << "eth_type=0x0800,ip_dst=" << ip_dst << " apply:nw_dec,output=" << port;
        DpctlExecute(swDpId, ss.str());
    }
    // Miss match packets will be sent to the other switch,
    // which is connected to the last port.
    std::stringstream ss;
    ss << "flow-mod cmd=add,table=0,prio=0 apply:output=" << (++port_cnt);
    DpctlExecute(swDpId, ss.str());

    m_switchPortCnt[swDpId - 1] = port_cnt; // Store switch total port count

    NS_LOG_INFO("[Controller][" << Simulator::Now().As(Time::S) << "] Configure switch " << swDpId
                                << " completed");
}

std::vector<uint32_t>
Controller::BP_LPT(std::vector<ScheduleRequest>& scheduleRequests)
{
    std::stringstream ss;
    ss << "[";
    for (const auto& req : scheduleRequests)
        ss << "(req_id=" << req.req_id << ", weight=" << req.weight << ")";
    ss << "]";
    NS_LOG_INFO("[Controller][" << Simulator::Now().As(Time::S) << "] Do BP-LPT for array(size="
                                << scheduleRequests.size() << "): " << ss.str());

    std::vector<uint32_t> ret;
    std::set<uint32_t> req_set;
    std::map<uint32_t, double> req_weight;
    for (const auto& req : scheduleRequests)
    {
        req_set.insert(req.req_id);
        req_weight[req.req_id] = req.weight;
    }
    while (!req_set.empty())
    {
        uint32_t sid = FindBottleneckServer(req_set);
        double mx = INFINITY;
        uint32_t target = 0;
        for (const auto& req_id : req_set)
        {
            double weight = req_weight[req_id] / SumKeyset(req_id, sid);
            NS_LOG_UNCOND("DEBUG: " << weight << " " << sid << " " << req_id << " "
                                    << req_weight[req_id] << " " << SumKeyset(req_id, sid));
            if (weight > mx)
                continue;
            mx = weight;
            target = req_id;
        }
        for (auto& [req_id, weight] : req_weight)
        {
            if (!req_set.count(req_id) || req_id == target)
                continue;
            uint32_t v1 = SumKeyset(req_id, sid);
            uint32_t v2 = SumKeyset(target, sid);
            if (v1 == 0 && v2 == 0)
                continue;
            weight -= (req_weight[target] * v1 / v2);
        }
        req_weight[target] = mx;
        ret.push_back(target);
        req_set.erase(target);
    }
    std::reverse(ret.begin(), ret.end());

    ss.str("");
    ss << "[";
    for (const auto& req_id : ret)
        ss << "(req_id=" << req_id << ", weight=" << req_weight[req_id] << ")";
    ss << "]";
    NS_LOG_INFO("[Controller][" << Simulator::Now().As(Time::S)
                                << "] Schedule completed(size=" << ret.size() << "): " << ss.str());

    return ret;
    // while (isScheduling)
    // {
    //     // Find the bottleneck server
    //     uint32_t keysetPlusQsz = 0, maxServer = 0;
    //     for (uint32_t server_id : heavyServerSet)
    //     {
    //         uint32_t val = (keysetTotalSize[server_id] + currentQueueSize[server_id]) /
    //                        m_serverRateList[server_id];
    //         if (val < keysetPlusQsz)
    //             continue;
    //         keysetPlusQsz = val;
    //         maxServer = server_id;
    //     }

    //     // Find the request with the largest weight
    //     double largestWeight = INT32_MIN;
    //     uint32_t largestWeightReqId = 0;
    //     for (const auto& packet : scheduleTmpPacket[maxServer])
    //     {
    //         uint32_t req_id = packet.sdnKvsHeader.GetReqId();
    //         double val = weightOfRequest[req_id] / packet.sdnKvsHeader.GetKsSiz(); // 初始化
    //         if (val <= largestWeight)
    //             continue;
    //         largestWeight = val;
    //         largestWeightReqId = req_id;
    //     }

    //     // Update request's weight
    //     auto cond = [largestWeightReqId](const PacketInput& packet) {
    //         return packet.sdnKvsHeader.GetReqId() == largestWeightReqId;
    //     };
    //     PacketInput target_packet = *std::find_if(scheduleTmpPacket[maxServer].begin(),
    //                                               scheduleTmpPacket[maxServer].end(),
    //                                               cond);
    //     uint32_t bottle_ks = target_packet.sdnKvsHeader.GetKsSiz();
    //     for (const auto& packet : scheduleTmpPacket[maxServer])
    //         if (packet.sdnKvsHeader.GetReqId() != largestWeightReqId)
    //             weightOfRequest[packet.sdnKvsHeader.GetReqId()] -=
    //                 weightOfRequest[largestWeightReqId] * packet.sdnKvsHeader.GetKsSiz() /
    //                 bottle_ks;

    //     // move request to stack
    //     for (auto& [server_id, vec] : scheduleTmpPacket)
    //     {
    //         if (currentQueueSize[server_id] == 0)
    //             break;
    //         auto erase_it = std::remove_if(vec.begin(), vec.end(), cond);
    //         for (auto it = erase_it; it != vec.end(); it++)
    //         {
    //             stackPacket[server_id].push(*it);
    //             keysetTotalSize[server_id] -= it->sdnKvsHeader.GetKsSiz();
    //         }
    //         vec.erase(erase_it, vec.end());
    //     }
    //     weightOfRequest.erase(largestWeightReqId);
    //     if (weightOfRequest.size() == 0)
    //         isScheduling = false;
    // }
}

void
Controller::DoSchedule(SdnKvsHeader& sdnKvsHeader,
                       struct ofl_msg_packet_in* msg,
                       Ptr<const RemoteSwitch> swtch,
                       uint32_t xid)
{
    std::stringstream ss;
    sdnKvsHeader.Print(ss);
    NS_LOG_INFO("[Controller][" << Simulator::Now().As(Time::S) << "] Schedule client packet"
                                << ss.str());
    // Get server IP address
    ss.str("");
    Ipv4Address serverIp = ExtractIpv4Address(OXM_OF_IPV4_DST, (struct ofl_match*)msg->match);
    ss << serverIp;

    // Build schedule packet
    SchedulePacket schedulePacket;
    schedulePacket.id = ++m_idCnt;
    schedulePacket.sdnKvsHeader = sdnKvsHeader;
    schedulePacket.msg = msg;
    schedulePacket.swtch = swtch;
    schedulePacket.xid = xid;
    schedulePacket.arriveTime = Simulator::Now(); // Record arriving time
    schedulePacket.dstServerId = m_ipToServer[ss.str()];

    // Push the schedule packet into the queue
    ScheduleContext& context = m_scheduleContexts[schedulePacket.dstServerId];
    context.schedulePackets.push_back(schedulePacket);

    std::map<uint32_t, int64_t> req_wait_time; // Map<request id, max wait time>
    for (auto& [server_id, ctx] : m_scheduleContexts)
    {
        if (ctx.schedulePackets.empty()) // Not overloaded
            continue;
        int64_t dt = Simulator::Now().GetMilliSeconds() - ctx.lastScheduleTime.GetMilliSeconds();
        if (dt >= 1.00) // 1ms is the minimum delta time.
        {
            uint32_t queue_size = ctx.queue_size;
            // Update each overloaded server's queue size
            ctx.queue_size = std::max(0U, uint32_t(queue_size - ctx.server_rate * dt));
            ctx.lastScheduleTime = Simulator::Now();
        }
        // Prepare for BP-LPT algorithm. Initialize all requests' wait times.
        for (auto& packet : ctx.schedulePackets)
        {
            uint32_t req_id = packet.sdnKvsHeader.GetReqId();
            int64_t wait_time =
                Simulator::Now().GetMilliSeconds() - packet.arriveTime.GetMilliSeconds();
            if (!req_wait_time.count(req_id))
                req_wait_time[req_id] = 0;
            req_wait_time[req_id] = std::max(req_wait_time[req_id], wait_time);
        }
    }

    // Waiting for the scheduling requests list. Sorted in scheduling.
    std::vector<ScheduleRequest> scheduleRequests;
    // Build request list
    for (const auto& [req_id, wait_time] : req_wait_time)
    {
        ScheduleRequest scheduleRequest;
        scheduleRequest.req_id = req_id;
        scheduleRequest.weight = 1.0 + 2.0 * wait_time;
        scheduleRequests.push_back(scheduleRequest);
    }
    // Do BP-LPT algorithm
    std::vector<uint32_t> sortedReuests = BP_LPT(scheduleRequests);

    std::map<uint32_t, std::vector<SchedulePacket*>> pktSnapshot;
    // Prebuild packet snapshot data structure
    for (auto& [server_id, ctx] : m_scheduleContexts)
    {
        for (auto& pkt : ctx.schedulePackets)
        {
            uint32_t req_id = pkt.sdnKvsHeader.GetReqId();
            if (!pktSnapshot.count(req_id))
                pktSnapshot[req_id] = std::vector<SchedulePacket*>();
            pktSnapshot[req_id].push_back(&pkt);
        }
    }

    for (const auto& req_id : sortedReuests)
    {
        for (const auto& pkt : pktSnapshot[req_id])
        {
            // Log SDN-KVS header information
            ss.str("");
            pkt->sdnKvsHeader.Print(ss);

            ScheduleContext& ctx = m_scheduleContexts[pkt->dstServerId];
            Time dt = Seconds((1.0 * ctx.queue_size) / (1000.0 * ctx.server_rate));
            // Remove send event set before
            if (pkt->eventId.IsRunning())
            {
                NS_LOG_INFO("[Controller][" << Simulator::Now().As(Time::S)
                                            << "] Cancel the send event for Packet(pkt_id="
                                            << pkt->id << ")" << ss.str());
                pkt->eventId.Cancel();
            }
            // Set send event
            pkt->eventId = Simulator::Schedule(dt, &Controller::RemovePacketAndForward, this, *pkt);
            NS_LOG_INFO("[Controller][" << Simulator::Now().As(Time::S)
                                        << "] Packet(pkt_id=" << pkt->id << ")" << ss.str()
                                        << " will be forwarded to the server " << pkt->dstServerId
                                        << " in " << dt.As(Time::S));
        }
    }
    // Do not free the message.
    // Message will be free in RemovePacketAndForward function
}

void
Controller::DoFlowModify(SdnKvsHeader& sdnKvsHeader,
                         struct ofl_msg_packet_in* msg,
                         Ptr<const RemoteSwitch> swtch,
                         uint32_t xid)
{
    if (sdnKvsHeader.GetHeavy() == 1)
    {
        std::stringstream ss;
        // Get the switch datapath ID
        uint64_t swDpId = swtch->GetDpId();
        // Get server IP address
        Ipv4Address serverIp = ExtractIpv4Address(OXM_OF_IPV4_SRC, (struct ofl_match*)msg->match);
        ss << serverIp;
        std::string serverIp_str = ss.str();
        uint32_t server_id = m_ipToServer[serverIp_str];

        // Record the overload time, queue size and server service rate when overloaded
        ScheduleContext& context = m_scheduleContexts[server_id];
        context.queue_size = sdnKvsHeader.GetQueueSiz();
        context.server_rate = sdnKvsHeader.GetServerRate();
        context.lastScheduleTime = Simulator::Now();

        // The server is overloaded. Modify the flow table to make each packet
        // sent from the client to be redirected to the controller.
        NS_LOG_INFO("[Controller][" << Simulator::Now().As(Time::S) << "] Server " << server_id
                                    << "(ip=" << serverIp_str
                                    << ", queue_siz=" << context.queue_size
                                    << ", heavy_time=" << context.lastScheduleTime.As(Time::S)
                                    << ", server_rate=" << context.server_rate
                                    << ") is overloaded. Modify flow table for switch " << swDpId);
        ss.str(""); // Clear string stream
        ss << "flow-mod cmd=mod,table=0,prio=100 "
           << "eth_type=0x0800,ip_dst=" << serverIp_str << " apply:output=ctrl:128";
        DpctlExecute(swDpId, ss.str());

        // Cancel the undo event if there is already an event set before.
        ScheduleContext& ctx = m_scheduleContexts[server_id];
        if (ctx.undoFlowEvents[swDpId].IsRunning())
        {
            NS_LOG_INFO("[Controller][" << Simulator::Now().As(Time::S)
                                        << "] Cancel undo flow table modification event for switch "
                                        << swDpId << " (server_id=" << server_id << ")");
            ctx.undoFlowEvents[swDpId].Cancel();
        }

        // Set timer to undo modification.
        Time dt =
            Seconds(1.0 * sdnKvsHeader.GetQueueSiz() / (1000.0 * sdnKvsHeader.GetServerRate()));
        NS_LOG_INFO("[Controller][" << Simulator::Now().As(Time::S) << "] " << dt.As(Time::S)
                                    << " later will undo the modification of the flow table");
        ctx.undoFlowEvents[swDpId] =
            Simulator::Schedule(dt, &Controller::UndoFlowModify, this, swDpId, serverIp_str);
    }
    // Forward packet to client
    ForwardPacket(msg, swtch, xid);
    // Free message after forwarding packet
    ofl_msg_free((struct ofl_msg_header*)msg, nullptr);
}

void
Controller::UndoFlowModify(uint32_t switch_id, std::string server_ip)
{
    uint32_t server_id = m_ipToServer[server_ip];
    int32_t port = GetSwitchPortByServerId(switch_id, server_id);
    if (port == -1)
        return; // Should be unreachable

    NS_LOG_INFO("[Controller][" << Simulator::Now().As(Time::S)
                                << "] Undo the modification of the flow table(switch_id="
                                << switch_id << ", server_ip=" << server_ip << ")");

    std::stringstream ss;
    ss << "flow-mod cmd=mod,table=0,prio=100 "
       << "eth_type=0x0800,ip_dst=" << server_ip << " apply:nw_dec,output=" << port;
    DpctlExecute(switch_id, ss.str());
}

void
Controller::RemovePacketAndForward(SchedulePacket pkt)
{
    // Print log
    std::stringstream ss;
    pkt.sdnKvsHeader.Print(ss);
    NS_LOG_INFO("[Controller][" << Simulator::Now().As(Time::S)
                                << "] Forward packet(pkt_id=" << pkt.id << ")" << ss.str()
                                << " to the server " << pkt.dstServerId);
    // Remove packet from schdule list
    ScheduleContext& ctx = m_scheduleContexts[pkt.dstServerId];
    auto cond = [&pkt](SchedulePacket& packet) { return packet.id == pkt.id; };
    auto it = std::find_if(ctx.schedulePackets.begin(), ctx.schedulePackets.end(), cond);
    if (it != ctx.schedulePackets.end())
        ctx.schedulePackets.erase(it);
    // Forward packet to the server
    ForwardPacket(pkt.msg, pkt.swtch, pkt.xid);
    // Free message after forwarding packet
    ofl_msg_free((struct ofl_msg_header*)pkt.msg, nullptr);
}

int
Controller::ForwardPacket(struct ofl_msg_packet_in* msg,
                          Ptr<const RemoteSwitch> swtch,
                          uint32_t xid)
{
    // Get the switch datapath ID
    uint64_t swDpId = swtch->GetDpId();
    // Get input port
    uint32_t inPort;
    struct ofl_match_tlv* tlv = oxm_match_lookup(OXM_OF_IN_PORT, (struct ofl_match*)msg->match);
    memcpy(&inPort, tlv->value, OXM_LENGTH(OXM_OF_IN_PORT));
    // Get destination IP address
    Ipv4Address dstIp = ExtractIpv4Address(OXM_OF_IPV4_DST, (struct ofl_match*)msg->match);
    // Get output port
    uint32_t outPort = GetSwitchPortByIPv4(swDpId, dstIp);

    struct ofl_action_output* action =
        (struct ofl_action_output*)xmalloc(sizeof(struct ofl_action_output));
    action->header.type = OFPAT_OUTPUT;
    action->port = outPort;
    action->max_len = 0;

    // Send the reply within an OpenFlow PacketOut message
    struct ofl_msg_packet_out reply;
    reply.header.type = OFPT_PACKET_OUT;
    reply.buffer_id = msg->buffer_id;
    reply.in_port = inPort;
    reply.actions_num = 1;
    reply.actions = (struct ofl_action_header**)&action;
    reply.data_length = 0;
    reply.data = nullptr;
    if (msg->buffer_id == NO_BUFFER)
    {
        // No packet buffer. Send data back to switch
        reply.data_length = msg->data_length;
        reply.data = msg->data;
    }

    int ret = SendToSwitch(swtch, (struct ofl_msg_header*)&reply, xid);
    NS_LOG_INFO("[Controller][" << Simulator::Now().As(Time::S)
                                << "] Sent Packet-Out packet: (buffer_id=" << reply.buffer_id
                                << ", in_port=" << reply.in_port << ", out_port=" << action->port
                                << ") to switch " << swtch->GetDpId());
    free(action);
    return ret;
}

std::string
Controller::GetIpv4AddressFromNode(Ptr<Node> node)
{
    std::stringstream ss;
    ss << node->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();
    return ss.str();
}

Ipv4Address
Controller::ExtractIpv4Address(uint32_t oxm_of, struct ofl_match* match)
{
    switch (oxm_of)
    {
    case static_cast<uint32_t>(OXM_OF_ARP_SPA):
    case static_cast<uint32_t>(OXM_OF_ARP_TPA):
    case static_cast<uint32_t>(OXM_OF_IPV4_DST):
    case static_cast<uint32_t>(OXM_OF_IPV4_SRC): {
        uint32_t ip;
        int size = OXM_LENGTH(oxm_of);
        struct ofl_match_tlv* tlv = oxm_match_lookup(oxm_of, match);
        memcpy(&ip, tlv->value, size);
        return Ipv4Address(ntohl(ip));
    }
    default:
        NS_ABORT_MSG("Invalid IP field.");
    }
}

int32_t
Controller::GetSwitchPortByClientId(uint64_t switch_id, uint32_t client_id)
{
    if (switch_id != client_id % 2 + 1)
        return -1;
    // even client_id: (port - 1) * 2 = client_id  ==>  port = client_id / 2 + 1
    // odd client_id: 2 * port - 1 = client_id  ==>  port = (client_id + 1) / 2
    if (client_id % 2 == 0)
        return client_id / 2 + 1;
    else
        return (client_id + 1) / 2;
}

int32_t
Controller::GetSwitchPortByServerId(uint64_t switch_id, uint32_t server_id)
{
    uint32_t client_num = m_clientNodes.size();
    uint32_t global_id = client_num + server_id;
    if (switch_id != global_id % 2 + 1)
        return -1;
    // even global_id: (port - 1) * 2 = global_id  ==>  port = global_id / 2 + 1
    // odd global_id: 2 * port - 1 = global_id  ==>  port = (global_id + 1) / 2
    if (global_id % 2 == 0)
        return global_id / 2 + 1;
    else
        return (global_id + 1) / 2;
}

int32_t
Controller::GetSwitchPortByIPv4(uint64_t switch_id, Ipv4Address& ipv4)
{
    std::stringstream ss;
    ss << ipv4;
    std::string ip = ss.str();
    if (m_ipToServer.count(ip))
    {
        uint32_t server_id = m_ipToServer[ip];
        int32_t ret = GetSwitchPortByServerId(switch_id, server_id);
        return (ret == -1) ? m_switchPortCnt[switch_id - 1] : ret;
    }
    uint32_t client_id = m_ipToClient[ip];
    int32_t ret = GetSwitchPortByClientId(switch_id, client_id);
    return (ret == -1) ? m_switchPortCnt[switch_id - 1] : ret;
}

uint32_t
Controller::SumKeyset(std::set<uint32_t>& req_set, uint32_t server_id)
{
    uint32_t ret = 0;
    const ScheduleContext& ctx = m_scheduleContexts[server_id];
    for (const auto& pkt : ctx.schedulePackets)
    {
        uint32_t req_id = pkt.sdnKvsHeader.GetReqId();
        if (!req_set.count(req_id))
            continue;
        ret += pkt.sdnKvsHeader.GetKsSiz();
    }
    return ret;
}

uint32_t
Controller::SumKeyset(uint32_t req_id, uint32_t server_id)
{
    uint32_t ret = 0;
    const ScheduleContext& ctx = m_scheduleContexts[server_id];
    for (const auto& pkt : ctx.schedulePackets)
    {
        if (pkt.sdnKvsHeader.GetReqId() != req_id)
            continue;
        ret += pkt.sdnKvsHeader.GetKsSiz();
    }
    return ret;
}

uint32_t
Controller::FindBottleneckServer(std::set<uint32_t>& req_set)
{
    // Assert that there is at least one server overloaded and can always get the answer
    // when this function is called.
    uint32_t mx = 0, ret = 0;
    uint32_t server_num = m_serverNodes.size();
    for (uint32_t i = 0; i < server_num; i++)
    {
        const ScheduleContext& ctx = m_scheduleContexts[i];
        if (ctx.schedulePackets.empty()) // This server is not overloaded
            continue;
        // The weight computed here is always greater than or equal to 0
        uint32_t weight = SumKeyset(req_set, i) + ctx.queue_size;
        if (weight < mx)
            continue;
        mx = weight, ret = i;
    }
    return ret;
}
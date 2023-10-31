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
        if (m_ipToClient.count(ss.str()))
            DoSchedule(sdnKvsHeader, msg, swtch, xid);
        else if (m_ipToServer.count(ss.str()))
            DoFlowModify(sdnKvsHeader, msg, swtch, xid);
    }
    else
    {
        NS_LOG_WARN("This controller can't handle the packet. Unexpected Packet-In reason.");
    }

    // All handlers must free the message when everything is ok
    ofl_msg_free((struct ofl_msg_header*)msg, nullptr);
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

void
Controller::DoSchedule(SdnKvsHeader& sdnKvsHeader,
                       struct ofl_msg_packet_in* msg,
                       Ptr<const RemoteSwitch> swtch,
                       uint32_t xid)
{
    NS_LOG_INFO("[Controller][" << Simulator::Now().As(Time::S)
                                << "] Schedule client packet, reqid " << sdnKvsHeader.GetReqId()
                                << " ksSize " << sdnKvsHeader.GetKsSiz());
    uint32_t server_num = m_serverNodes.size();
    for (uint32_t i = 0; i < server_num; i++)
        m_vecPacket[i] = std::vector<PacketInput>();
    // Get server IP address
    std::stringstream ss;
    Ipv4Address serverIp = ExtractIpv4Address(OXM_OF_IPV4_DST, (struct ofl_match*)msg->match);
    Ipv4Address clientIp = ExtractIpv4Address(OXM_OF_IPV4_SRC, (struct ofl_match*)msg->match);
    ss << serverIp;

    PacketInput packet_input;
    packet_input.sdnKvsHeader = sdnKvsHeader;
    packet_input.msg = msg;
    packet_input.swtch = swtch;
    packet_input.xid = xid;
    packet_input.arrive_time = Simulator::Now();
    packet_input.from = Address(clientIp);
    m_vecPacket[m_ipToServer[ss.str()]].push_back(packet_input);

    // LRPT_last schedule
    bool isScheduling = true;
    double weight_max = 2.0;
    std::map<uint32_t, double> weightOfRequest;   // Map<server id, request weight>
    std::map<uint32_t, uint64_t> keysetTotalSize; // Map<server id, total size>
    std::map<uint32_t, int64_t> currentQueueSize; // Map<server id ,current queue size>
    std::map<uint32_t, double> waitTime;          // Map<reqid, wait time>
    std::map<uint32_t, std::stack<PacketInput>> stackPacket;
    // init tmp packet for schedule
    std::map<uint32_t, std::vector<PacketInput>> scheduleTmpPacket(m_vecPacket);

    // update heavy server queue size
    for (const auto& [server_id, quque_size] : m_serverHeavyQueueSize)
    {
        currentQueueSize[server_id] =
            quque_size - (int64_t)m_serverRateList[server_id] *
                             (Simulator::Now().GetMilliSeconds() - m_serverHeavyTime[server_id]);
        if (currentQueueSize[server_id] < 0)
            currentQueueSize[server_id] = 0;
    }

    for (const auto& [server_id, vec] : scheduleTmpPacket)
    {
        for (const auto& packet : vec)
        {
            uint32_t req_id = packet.sdnKvsHeader.GetReqId();
            // update request waitTime
            waitTime[req_id] =
                std::max(waitTime[req_id], (Simulator::Now() - packet.arrive_time).GetSeconds());
            keysetTotalSize[server_id] += packet.sdnKvsHeader.GetKsSiz();
            // init request weight
            weightOfRequest[req_id] = std::min(1 + 2.0 * waitTime[req_id], weight_max);
        }
    }

    while (isScheduling == true)
    {
        // Find the bottleneck server
        std::vector<uint32_t> keysetPlusQsz(server_num);
        for (uint32_t i = 0; i < server_num && keysetTotalSize[i] != 0; i++)
            keysetPlusQsz[i] = (keysetTotalSize[i] + currentQueueSize[i]) / m_serverRateList[i];
        uint32_t maxServer =
            max_element(keysetPlusQsz.begin(), keysetPlusQsz.end()) - keysetPlusQsz.begin();

        // Find the request with the largest weight
        double largestWeight = INT32_MIN;
        uint32_t largestWeightReqId = 0;
        for (const auto& packet : scheduleTmpPacket[maxServer])
        {
            uint32_t req_id = packet.sdnKvsHeader.GetReqId();
            double val = weightOfRequest[req_id] / packet.sdnKvsHeader.GetKsSiz(); // 初始化
            if (val <= largestWeight)
                continue;
            largestWeight = val;
            largestWeightReqId = req_id;
        }

        // Update request's weight
        auto cond = [largestWeightReqId](const PacketInput& packet) {
            return packet.sdnKvsHeader.GetReqId() == largestWeightReqId;
        };
        PacketInput target_packet = *std::find_if(scheduleTmpPacket[maxServer].begin(),
                                                  scheduleTmpPacket[maxServer].end(),
                                                  cond);
        uint32_t bottle_ks = target_packet.sdnKvsHeader.GetKsSiz();
        for (const auto& packet : scheduleTmpPacket[maxServer])
            if (packet.sdnKvsHeader.GetReqId() != largestWeightReqId)
                weightOfRequest[packet.sdnKvsHeader.GetReqId()] -=
                    weightOfRequest[largestWeightReqId] * packet.sdnKvsHeader.GetKsSiz() /
                    bottle_ks;

        // move request to stack
        for (auto& [server_id, vec] : scheduleTmpPacket)
        {
            if (currentQueueSize[server_id] == 0)
                break;
            auto erase_it = std::remove_if(vec.begin(), vec.end(), cond);
            for (auto it = erase_it; it != vec.end(); it++)
            {
                stackPacket[server_id].push(*it);
                keysetTotalSize[server_id] -= it->sdnKvsHeader.GetKsSiz();
            }
            vec.erase(erase_it, vec.end());
        }
        weightOfRequest.erase(largestWeightReqId);
        if (weightOfRequest.size() == 0)
            isScheduling = false;
    }
    // end schedule

    // remove send event
    auto event_cond = [&currentQueueSize](SendEvent& event) {
        return event.eventId.IsExpired() || currentQueueSize[event.serverId] != 0;
    };
    auto event_it = std::remove_if(m_sendEventList.begin(), m_sendEventList.end(), event_cond);
    for (auto it = event_it; it != m_sendEventList.end(); it++)
        if (currentQueueSize[it->serverId] != 0)
            it->eventId.Cancel();
    m_sendEventList.erase(event_it, m_sendEventList.end());

    // set send event
    for (auto& [server_id, st] : stackPacket)
    {
        if (st.empty())
            continue;
        double tmp = (1.0 * currentQueueSize[server_id]) / (1.0 * m_serverRateList[server_id]);
        while (!st.empty())
        {
            PacketInput packet = st.top();
            Time t = Seconds(tmp / 1000);
            ss.str("");
            ss << packet.from;
            SendEvent sendE;
            sendE.eventId = Simulator::Schedule(t,
                                                &Controller::RemovePacketAndForward,
                                                this,
                                                packet.sdnKvsHeader,
                                                packet.msg,
                                                packet.swtch,
                                                packet.xid);
            sendE.serverId = m_ipToServer[ss.str()];
            m_sendEventList.push_back(sendE);
            tmp += 0.05;
            st.pop();
        }
    }

    // ForwardPacket(msg, swtch, xid);
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

        // The server is overloaded. Record the overload time, queue length during overload, and
        // server service rate after server overload
        NS_LOG_INFO("[Controller][" << Simulator::Now().As(Time::MS) << "] Server "
                                    << m_ipToServer[serverIp_str] << " record QueueSiz "
                                    << sdnKvsHeader.GetQueueSiz() << " HeavyTime "
                                    << Simulator::Now().GetMilliSeconds() << " ServerRate "
                                    << sdnKvsHeader.GetServerRate());
        m_serverHeavyQueueSize[m_ipToServer[serverIp_str]] = sdnKvsHeader.GetQueueSiz();
        m_serverHeavyTime[m_ipToServer[serverIp_str]] = Simulator::Now().GetMilliSeconds();
        m_serverRateList[m_ipToServer[serverIp_str]] = sdnKvsHeader.GetServerRate();

        // The server is overloaded. Modify the flow table to make each packet
        // sent from the client to be redirected to the controller.
        NS_LOG_INFO("[Controller][" << Simulator::Now().As(Time::S) << "] Server "
                                    << m_ipToServer[serverIp_str] << "(" << serverIp_str
                                    << ") is overloaded. Modify flow table for switch " << swDpId);
        ss.str(""); // Clear string stream
        ss << "flow-mod cmd=mod,table=0,prio=100 "
           << "eth_type=0x0800,ip_dst=" << serverIp_str << " apply:output=ctrl:128";
        DpctlExecute(swDpId, ss.str());

        // Set timer to undo modification.
        Time dt =
            Seconds(1.0 * sdnKvsHeader.GetQueueSiz() / (1000.0 * sdnKvsHeader.GetServerRate()));
        NS_LOG_INFO("[Controller][" << Simulator::Now().As(Time::S) << "] " << dt.As(Time::S)
                                    << " later will undo the modification of the flow table");
        Simulator::Schedule(dt, &Controller::UndoFlowModify, this, swDpId, serverIp_str);
    }
    // Forward packet to client
    ForwardPacket(msg, swtch, xid);
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
Controller::RemovePacketAndForward(SdnKvsHeader& sdnKvsHeader,
                                   struct ofl_msg_packet_in* msg,
                                   Ptr<const RemoteSwitch> swtch,
                                   uint32_t xid)
{
    Address from = Address(ExtractIpv4Address(OXM_OF_IPV4_SRC, (struct ofl_match*)msg->match));
    for (auto& [server_id, vec] : m_vecPacket)
    {
        auto cond = [&from, msg](PacketInput& packet) {
            return msg->buffer_id == packet.msg->buffer_id && from == packet.from;
        };
        auto it = std::remove_if(vec.begin(), vec.end(), cond);
        vec.erase(it, vec.end());
    }
    ForwardPacket(msg, swtch, xid);
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
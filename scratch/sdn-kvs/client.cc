#include "client.h"

#include "sdn-kvs-header.h"

#include "ns3/boolean.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/ipv4-address.h"
#include "ns3/ipv6-address.h"
#include "ns3/log.h"
#include "ns3/nstime.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/socket.h"
#include "ns3/trace-source-accessor.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("ClientApplication");

NS_OBJECT_ENSURE_REGISTERED(Client);

TypeId
Client::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::Client")
            .SetParent<Application>()
            .SetGroupName("SDN-KVS")
            .AddConstructor<Client>()
            .AddTraceSource("Tx",
                            "A new packet is created and is sent",
                            MakeTraceSourceAccessor(&Client::m_txTrace),
                            "ns3::Packet::TracedCallback")
            .AddAttribute("SbfEnabled",
                          "Whether SBF algorithm is enabled on the client.",
                          BooleanValue(false),
                          MakeBooleanAccessor(&Client::m_sbf_enabled),
                          MakeBooleanChecker())
            .AddAttribute("DasEnabled",
                          "Whether DAS algorithm is enabled on the client.",
                          BooleanValue(false),
                          MakeBooleanAccessor(&Client::m_das_enabled),
                          MakeBooleanChecker())
            .AddTraceSource("Rx",
                            "A packet has been received",
                            MakeTraceSourceAccessor(&Client::m_rxTrace),
                            "ns3::Packet::TracedCallback")
            .AddTraceSource("TxWithAddresses",
                            "A new packet is created and is sent",
                            MakeTraceSourceAccessor(&Client::m_txTraceWithAddresses),
                            "ns3::Packet::TwoAddressTracedCallback")
            .AddTraceSource("RxWithAddresses",
                            "A packet has been received",
                            MakeTraceSourceAccessor(&Client::m_rxTraceWithAddresses),
                            "ns3::Packet::TwoAddressTracedCallback");
    return tid;
}

Client::Client()
{
}

Client::~Client()
{
}

void
Client::DoDispose()
{
    Application::DoDispose();
}

void
Client::StartApplication()
{
    if (m_das_enabled)
        ScheduleRequestEvent();
    else
        ScheduleTransmit();
}

void
Client::StopApplication()
{
}

void
Client::ScheduleTransmit()
{
    if (m_cur == m_request_list.size())
    {
        NS_LOG_INFO("[Client][" << Simulator::Now().As(Time::S) << "] All requests has been sent");
        return;
    }

    double lst = m_cur == 0 ? 0 : m_request_list[m_cur - 1].st;
    RequestTableEntry entry = m_request_list[m_cur++];
    Time dt = Seconds(entry.st - lst);

    SdnKvsHeader sdnKvsHeader;
    sdnKvsHeader.SetReqId(entry.req_id);
    sdnKvsHeader.SetKsSiz(entry.ks_siz);
    sdnKvsHeader.SetQueueSiz(0);
    sdnKvsHeader.SetServerRate(0);
    sdnKvsHeader.SetHeavy(0);
    sdnKvsHeader.SetBottleneckKsSiz(m_sbf_enabled ? entry.bottleneck_ks_size : 0);
    // DAS is not enabled
    sdnKvsHeader.SetPriority(entry.ks_siz);

    Ptr<Packet> packet = Create<Packet>();
    packet->AddHeader(sdnKvsHeader);

    Simulator::Schedule(dt, &Client::Send, this, packet, entry.dst_ip, entry.dst_port);
}

void
Client::SetRequestList(std::vector<RequestTableEntry>& request_list)
{
    m_request_list = request_list;
}

void
Client::Send(Ptr<Packet> packet, std::string dst_ip, uint16_t dst_port)
{
    Address peerAddress(Ipv4Address(dst_ip.c_str()));
    Ptr<Socket> socket = Connect(peerAddress, dst_port);
    Address localAddress;
    socket->GetSockName(localAddress);

    // call to the trace sinks before the packet is actually sent,
    // so that tags added to the packet can be sent as well
    m_txTrace(packet);
    m_txTraceWithAddresses(packet,
                           localAddress,
                           InetSocketAddress(Ipv4Address::ConvertFrom(peerAddress), dst_port));

    socket->Send(packet);

    SdnKvsHeader sdnKvsHeader;
    packet->PeekHeader(sdnKvsHeader);
    std::stringstream ss;
    sdnKvsHeader.Print(ss);
    NS_LOG_INFO("[Client][" << Simulator::Now().As(Time::S) << "] Sent SDN-KVS packet" << ss.str()
                            << " to " << Ipv4Address::ConvertFrom(peerAddress) << " port "
                            << dst_port);
    ScheduleTransmit();
}

Ptr<Socket>
Client::Connect(Address peerAddress, uint16_t peerPort)
{
    TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
    Ptr<Socket> socket = Socket::CreateSocket(GetNode(), tid);

    if (Ipv4Address::IsMatchingType(peerAddress))
    {
        if (socket->Bind() == -1)
            NS_FATAL_ERROR("Failed to bind socket");
        socket->Connect(InetSocketAddress(Ipv4Address::ConvertFrom(peerAddress), peerPort));
    }
    else if (Ipv6Address::IsMatchingType(peerAddress))
    {
        if (socket->Bind6() == -1)
            NS_FATAL_ERROR("Failed to bind socket");
        socket->Connect(Inet6SocketAddress(Ipv6Address::ConvertFrom(peerAddress), peerPort));
    }
    else if (InetSocketAddress::IsMatchingType(peerAddress))
    {
        if (socket->Bind() == -1)
            NS_FATAL_ERROR("Failed to bind socket");
        socket->Connect(peerAddress);
    }
    else if (Inet6SocketAddress::IsMatchingType(peerAddress))
    {
        if (socket->Bind6() == -1)
            NS_FATAL_ERROR("Failed to bind socket");
        socket->Connect(peerAddress);
    }
    else
    {
        NS_ASSERT_MSG(false, "Incompatible address type: " << peerAddress);
    }
    socket->SetRecvCallback(MakeCallback(&Client::HandleRead, this));
    socket->SetAllowBroadcast(true);

    return socket;
}

void
Client::ScheduleRequestEvent()
{
    std::map<uint32_t, Request> requests;
    for (const auto& entry : m_request_list)
    {
        if (!requests.count(entry.req_id))
        {
            Request request;
            request.st = entry.st;
            request.src_client = entry.src_client;

            request.req_id = entry.req_id;
            request.keysets = std::vector<Keyset>();
            requests[entry.req_id] = request;
        }
        Keyset ks;
        ks.dst_server = entry.dst_server;
        ks.dst_ip = entry.dst_ip;
        ks.dst_port = entry.dst_port;
        ks.ks_siz = entry.ks_siz;
        ks.bottleneck_ks_size = entry.bottleneck_ks_size;
        ks.is_sent = false;
        requests[entry.req_id].keysets.push_back(ks);
        // Add server information
        if (!m_servers.count(ks.dst_server))
        {
            m_servers[ks.dst_server] = Server{0, 1, 1};
            m_ipToServer[ks.dst_ip] = ks.dst_server;
        }
    }
    for (const auto& [req_id, req] : requests)
    {
        Simulator::Schedule(Seconds(req.st), &Client::HandleRequestEvent, this, req);
    }
}

void
Client::HandleRequestEvent(Request request)
{
    m_request_vec.push_back(request);
    // SortRequests();
    for (auto& [server_id, server] : m_servers)
    {
        if (server.osk)
            continue;
        SendOne(server_id);
    }
}

void
Client::SortRequests()
{
    std::set<Request> req_set;
    std::map<uint32_t, double> req_weight;
    for (auto& req : m_request_vec)
    {
        req_set.insert(req);
        req_weight[req.req_id] = 1;
    }
    m_request_vec.clear();
    while (!req_set.empty())
    {
        // Find server with the largest load
        uint32_t server_id = FindBottleneckServer(req_set);
        // Select weighted largest request to be served at last at server b
        double mn = INFINITY;
        Request target_req;
        for (const auto& req : req_set)
        {
            double val = req_weight[req.req_id] / SumKeyset(req.req_id, server_id);
            if (val < mn)
                mn = val, target_req = req;
        }
        //  Scale the weights
        double target_weight = req_weight[target_req.req_id];
        for (auto& [req_id, weight] : req_weight)
        {
            weight -= (target_weight * SumKeyset(req_id, server_id) /
                       SumKeyset(target_req.req_id, server_id));
        }
        //  Update the set of unscheduled requests for Iteration
        m_request_vec.push_back(target_req);
        req_set.erase(target_req);
    }
}

uint32_t
Client::SumKeyset(uint32_t req_id, uint32_t server_id)
{
    uint32_t ret = 0;
    for (const auto& req : m_request_vec)
    {
        if (req.req_id != req_id)
            continue;
        for (const auto& ks : req.keysets)
        {
            if (ks.dst_server != server_id)
                continue;
            if (ks.is_sent)
                continue;
            ret += ks.ks_siz;
        }
        break;
    }
    return ret;
}

uint32_t
Client::FindBottleneckServer(std::set<Request>& req_set)
{
    double mx = -1;
    uint32_t ret = -1;
    for (const auto& [server_id, server] : m_servers)
    {
        double sum = 0;
        for (const auto& req : req_set)
        {
            for (const auto& ks : req.keysets)
            {
                if (ks.dst_server != server_id)
                    continue;
                if (ks.is_sent)
                    continue;
                sum += (1.0 * ks.ks_siz) / m_servers[server_id].capacity;
            }
        }
        if (sum < mx)
            continue;
        mx = sum, ret = server_id;
    }
    return ret;
}

void
Client::SendOne(uint32_t server_id)
{
    for (auto& req : m_request_vec)
    {
        for (auto& ks : req.keysets)
        {
            if (ks.dst_server != server_id)
                continue;
            if (ks.is_sent)
                continue;
            // Build header
            SdnKvsHeader sdnKvsHeader;
            sdnKvsHeader.SetReqId(req.req_id);
            sdnKvsHeader.SetKsSiz(ks.ks_siz);
            sdnKvsHeader.SetQueueSiz(0);
            sdnKvsHeader.SetServerRate(0);
            sdnKvsHeader.SetHeavy(0);
            sdnKvsHeader.SetBottleneckKsSiz(0);
            sdnKvsHeader.SetPriority(AssignPriority(req));
            // Build packet
            Ptr<Packet> packet = Create<Packet>();
            packet->AddHeader(sdnKvsHeader);
            // Send packet
            Send(packet, ks.dst_ip, ks.dst_port);
            m_ksSendTime[std::pair<uint32_t, uint32_t>(req.req_id, ks.dst_server)] =
                Simulator::Now();
            ks.is_sent = true;
            // Update server information
            m_servers[server_id].osk = 1;
            return;
        }
    }
}

uint32_t
Client::AssignPriority(Request req)
{
    double mx = -1;
    for (const auto& ks : req.keysets)
    {
        double val = 1.0 * ks.ks_siz / m_servers[ks.dst_server].server_rate;
        if (val > mx)
            mx = val;
    }
    return uint32_t(1000 / mx);
}

void
Client::HandleRead(Ptr<Socket> socket)
{
    Ptr<Packet> packet;
    Address from;
    Address localAddress;
    while ((packet = socket->RecvFrom(from)))
    {
        SdnKvsHeader sdnKvsHeader;
        packet->PeekHeader(sdnKvsHeader);
        std::stringstream ss;
        if (m_das_enabled)
        {
            double alpha = 0.1;
            double beta = 0.1;
            InetSocketAddress::ConvertFrom(from).GetIpv4().Print(ss);
            uint32_t server_id = m_ipToServer[ss.str()];
            m_servers[server_id].osk = 0;
            m_servers[server_id].server_rate =
                beta * m_servers[server_id].server_rate + (1 - beta) * sdnKvsHeader.GetServerRate();
            auto key = std::pair<uint32_t, uint32_t>(sdnKvsHeader.GetReqId(), server_id);
            Time send_time = m_ksSendTime[key];
            Time recv_time = Simulator::Now();
            int64_t dt = recv_time.GetMilliSeconds() - send_time.GetMilliSeconds();
            m_servers[server_id].capacity = alpha * m_servers[server_id].capacity +
                                            (1 - alpha) * (sdnKvsHeader.GetKsSiz() / dt);
            // Avoid map being much large
            m_ksSendTime.erase(key);
            SendOne(server_id);
        }
        ss.str("");
        sdnKvsHeader.Print(ss);
        if (InetSocketAddress::IsMatchingType(from))
        {
            NS_LOG_INFO("[Client][" << Simulator::Now().As(Time::S) << "] Received SDN-KVS packet"
                                    << ss.str() << " from "
                                    << InetSocketAddress::ConvertFrom(from).GetIpv4() << " port "
                                    << InetSocketAddress::ConvertFrom(from).GetPort());
        }
        else if (Inet6SocketAddress::IsMatchingType(from))
        {
            NS_LOG_INFO("[Client][" << Simulator::Now().As(Time::S) << "] Received SDN-KVS packet"
                                    << ss.str() << " from "
                                    << Inet6SocketAddress::ConvertFrom(from).GetIpv6() << " port "
                                    << Inet6SocketAddress::ConvertFrom(from).GetPort());
        }
        socket->GetSockName(localAddress);
        m_rxTrace(packet);
        m_rxTraceWithAddresses(packet, from, localAddress);
    }

    // Completed
    socket->Close();
    socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
    socket = nullptr; // Make pointer counter be zero
}

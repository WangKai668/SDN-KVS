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
                          "Whether SRPT algorithm is enabled on the server.",
                          BooleanValue(false),
                          MakeBooleanAccessor(&Client::m_sbf_enabled),
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

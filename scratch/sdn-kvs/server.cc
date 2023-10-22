#include "server.h"

#include "sdn-kvs-header.h"

#include "ns3/address-utils.h"
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
#include "ns3/udp-socket.h"
#include "ns3/uinteger.h"

#include <sstream>

NS_LOG_COMPONENT_DEFINE("ServerApplication");
NS_OBJECT_ENSURE_REGISTERED(Server);

TypeId
Server::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::Server") // MODIFY LINE
            .SetParent<Application>()
            .SetGroupName("SDN-KVS")  // MODIFY LINE
            .AddConstructor<Server>() // MODIFY LINE
            .AddAttribute("Port",
                          "Port on which we listen for incoming packets.",
                          UintegerValue(9),
                          MakeUintegerAccessor(&Server::m_port),
                          MakeUintegerChecker<uint16_t>())
            // MODIFY BEGIN
            .AddAttribute("ServerRate",
                          "Server handle rate.",
                          UintegerValue(5),
                          MakeUintegerAccessor(&Server::m_server_rate),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("TQS",
                          "Server TQS. Send overloaded signal packet to controller when queue size "
                          "is greater than TQS",
                          UintegerValue(64),
                          MakeUintegerAccessor(&Server::m_tqs),
                          MakeUintegerChecker<uint32_t>())
            // MODIFY END
            .AddTraceSource("Rx",
                            "A packet has been received",
                            MakeTraceSourceAccessor(&Server::m_rxTrace),
                            "ns3::Packet::TracedCallback")
            .AddTraceSource("RxWithAddresses",
                            "A packet has been received",
                            MakeTraceSourceAccessor(&Server::m_rxTraceWithAddresses),
                            "ns3::Packet::TwoAddressTracedCallback");
    return tid;
}

Server::Server()
{
    NS_LOG_FUNCTION(this);
    m_queue_size = 0; // MODIFY LINE
}

Server::~Server()
{
    NS_LOG_FUNCTION(this);
    m_socket = nullptr;
    m_socket6 = nullptr;
}

void
Server::DoDispose()
{
    NS_LOG_FUNCTION(this);
    Application::DoDispose();
}

void
Server::StartApplication()
{
    NS_LOG_FUNCTION(this);

    if (!m_socket)
    {
        TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
        m_socket = Socket::CreateSocket(GetNode(), tid);
        InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), m_port);
        if (m_socket->Bind(local) == -1)
        {
            NS_FATAL_ERROR("Failed to bind socket");
        }
        if (addressUtils::IsMulticast(m_local))
        {
            Ptr<UdpSocket> udpSocket = DynamicCast<UdpSocket>(m_socket);
            if (udpSocket)
            {
                // equivalent to setsockopt (MCAST_JOIN_GROUP)
                udpSocket->MulticastJoinGroup(0, m_local);
            }
            else
            {
                NS_FATAL_ERROR("Error: Failed to join multicast group");
            }
        }
    }

    if (!m_socket6)
    {
        TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
        m_socket6 = Socket::CreateSocket(GetNode(), tid);
        Inet6SocketAddress local6 = Inet6SocketAddress(Ipv6Address::GetAny(), m_port);
        if (m_socket6->Bind(local6) == -1)
        {
            NS_FATAL_ERROR("Failed to bind socket");
        }
        if (addressUtils::IsMulticast(local6))
        {
            Ptr<UdpSocket> udpSocket = DynamicCast<UdpSocket>(m_socket6);
            if (udpSocket)
            {
                // equivalent to setsockopt (MCAST_JOIN_GROUP)
                udpSocket->MulticastJoinGroup(0, local6);
            }
            else
            {
                NS_FATAL_ERROR("Error: Failed to join multicast group");
            }
        }
    }

    m_socket->SetRecvCallback(MakeCallback(&Server::HandleRead, this));
    m_socket6->SetRecvCallback(MakeCallback(&Server::HandleRead, this));
}

void
Server::StopApplication()
{
    NS_LOG_FUNCTION(this);

    if (m_socket)
    {
        m_socket->Close();
        m_socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
    }
    if (m_socket6)
    {
        m_socket6->Close();
        m_socket6->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
    }
}

void
Server::HandleRead(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);

    Ptr<Packet> packet;
    Address from;
    Address localAddress;
    while ((packet = socket->RecvFrom(from)))
    {
        socket->GetSockName(localAddress);
        m_rxTrace(packet);
        m_rxTraceWithAddresses(packet, from, localAddress);
        if (InetSocketAddress::IsMatchingType(from))
        {
            NS_LOG_INFO("At time " << Simulator::Now().As(Time::S) << " server received "
                                   << packet->GetSize() << " bytes from "
                                   << InetSocketAddress::ConvertFrom(from).GetIpv4() << " port "
                                   << InetSocketAddress::ConvertFrom(from).GetPort());
        }
        else if (Inet6SocketAddress::IsMatchingType(from))
        {
            NS_LOG_INFO("At time " << Simulator::Now().As(Time::S) << " server received "
                                   << packet->GetSize() << " bytes from "
                                   << Inet6SocketAddress::ConvertFrom(from).GetIpv6() << " port "
                                   << Inet6SocketAddress::ConvertFrom(from).GetPort());
        }

        packet->RemoveAllPacketTags();
        packet->RemoveAllByteTags();

        // MODIFY BEGIN

        uint32_t ks_siz = HandlePacket(packet); // Hook

        // Schedule send packet event
        Time dt = Seconds(1.0 * ks_siz / m_server_rate);
        Simulator::Schedule(dt, &Server::SendPacket, this, socket, packet, from, ks_siz);

        // MODIFY END
    }
}

uint32_t
Server::HandlePacket(Ptr<Packet> packet)
{
    NS_LOG_FUNCTION(this << packet);

    // Deserialize SDN-KVS header
    SdnKvsHeader sdnKvsHeader;
    packet->RemoveHeader(sdnKvsHeader);

    // Print packet header for debug
    std::stringstream ss;
    sdnKvsHeader.Print(ss);
    NS_LOG_DEBUG("SDN-KVS Packet: " << ss.str());

    uint32_t ks_siz = sdnKvsHeader.GetKsSiz();
    m_queue_size += ks_siz;
    sdnKvsHeader.SetHeavy(0);
    if (m_queue_size > m_tqs)
    {
        // Server overloaded, send signal packet to SDN controller
        NS_LOG_UNCOND("Server overloaded, set heavy field to 1"
                      << "(queue_siz = " << m_queue_size << ", tqs = " << m_tqs << ")");
        sdnKvsHeader.SetHeavy(1U);
    }
    sdnKvsHeader.SetQueueSiz(m_queue_size);
    sdnKvsHeader.SetServerRate(m_server_rate);

    packet->AddHeader(sdnKvsHeader);
    return ks_siz;
}

void
Server::SendPacket(Ptr<Socket> socket, Ptr<Packet> packet, Address& from, uint32_t ks_siz)
{
    NS_LOG_FUNCTION(this << socket << packet << from << ks_siz);

    m_queue_size -= ks_siz; // MODIFY LINE: Pop the packet from server queue before sending
    socket->SendTo(packet, 0, from);

    if (InetSocketAddress::IsMatchingType(from))
    {
        NS_LOG_INFO("At time " << Simulator::Now().As(Time::S) << " server sent "
                               << packet->GetSize() << " bytes to "
                               << InetSocketAddress::ConvertFrom(from).GetIpv4() << " port "
                               << InetSocketAddress::ConvertFrom(from).GetPort());
    }
    else if (Inet6SocketAddress::IsMatchingType(from))
    {
        NS_LOG_INFO("At time " << Simulator::Now().As(Time::S) << " server sent "
                               << packet->GetSize() << " bytes to "
                               << Inet6SocketAddress::ConvertFrom(from).GetIpv6() << " port "
                               << Inet6SocketAddress::ConvertFrom(from).GetPort());
    }
}
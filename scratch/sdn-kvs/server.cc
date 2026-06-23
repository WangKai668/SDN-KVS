#include "server.h"

#include "sdn-kvs-header.h"

#include "ns3/address-utils.h"
#include "ns3/boolean.h"
#include "ns3/double.h"
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
            .AddAttribute("ServerId",
                          "Server ID.",
                          UintegerValue(0),
                          MakeUintegerAccessor(&Server::m_server_id),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("ServerRate",
                          "Server handle rate.",
                          UintegerValue(1),
                          MakeUintegerAccessor(&Server::m_server_rate),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("TQS",
                          "Server TQS. Send overloaded signal packet to controller when queue size "
                          "is greater than TQS",
                          UintegerValue(64),
                          MakeUintegerAccessor(&Server::m_tqs),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("F_tqs_dynamic",
                          "  ",
                          UintegerValue(0),
                          MakeUintegerAccessor(&Server::f_tqs_dynamic),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("SrptEnabled",
                          "Whether SRPT algorithm is enabled on the server.",
                          BooleanValue(false),
                          MakeBooleanAccessor(&Server::m_srpt_enabled),
                          MakeBooleanChecker())
            .AddAttribute("SbfEnabled",
                          "Whether SBF algorithm is enabled on the server.",
                          BooleanValue(false),
                          MakeBooleanAccessor(&Server::m_sbf_enabled),
                          MakeBooleanChecker())
            .AddAttribute("SdnKvsEnabled",
                          "Whether SDN-KVS algorithm is enabled on the server.",
                          BooleanValue(false),
                          MakeBooleanAccessor(&Server::m_sdn_kvs_enabled),
                          MakeBooleanChecker())
            .AddAttribute("SRavg",
                          "Average server rate.",
                          DoubleValue(1.0),
                          MakeDoubleAccessor(&Server::m_SR_avg),
                          MakeDoubleChecker<double>())
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
    // MODIFY BEGIN
    m_queue_size = 0;
    m_lst = 0;
    m_maxq_lst = 0;
    // MODIFY END
}

Server::~Server()
{
    m_socket = nullptr;
    m_socket6 = nullptr;
}

void
Server::DoDispose()
{
    Application::DoDispose();
}

void
Server::StartApplication()
{
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
    Ptr<Packet> packet;
    Address from;
    Address localAddress;
    while ((packet = socket->RecvFrom(from)))
    {
        socket->GetSockName(localAddress);
        m_rxTrace(packet);
        m_rxTraceWithAddresses(packet, from, localAddress);

        SdnKvsHeader sdnKvsHeader;
        packet->PeekHeader(sdnKvsHeader);
        std::stringstream ss;
        sdnKvsHeader.Print(ss);
        if (InetSocketAddress::IsMatchingType(from))
        {
            std::cout << "[Server][" << Simulator::Now().As(Time::S) << "] Server " << m_server_id
                      << "(queue_siz=" << m_queue_size
                      << ", update_time=" << MilliSeconds(m_lst).As(Time::S)
                      << ") received SDN-KVS packet" << ss.str() << " from "
                      << InetSocketAddress::ConvertFrom(from).GetIpv4() << " port "
                      << InetSocketAddress::ConvertFrom(from).GetPort() << std::endl;
        }
        else if (Inet6SocketAddress::IsMatchingType(from))
        {
            std::cout << "[Server][" << Simulator::Now().As(Time::S) << "] Server " << m_server_id
                      << "(queue_siz=" << m_queue_size
                      << ", update_time=" << MilliSeconds(m_lst).As(Time::S)
                      << ") received SDN-KVS packet" << ss.str() << " from "
                      << Inet6SocketAddress::ConvertFrom(from).GetIpv6() << " port "
                      << Inet6SocketAddress::ConvertFrom(from).GetPort() << std::endl;
        }

        packet->RemoveAllPacketTags();
        packet->RemoveAllByteTags();

        // MODIFY BEGIN
        if (m_srpt_enabled)
        {
            m_queue_size += sdnKvsHeader.GetKsSiz();
            // Build schedule context
            ScheduleContext ctx =
                {Simulator::Now(), sdnKvsHeader, socket, packet, from, ServerSchedulingType::SRPT};
            // Push the context into the queue
            m_queue.push(ctx);
            if (!m_cur_event.IsRunning())
            {
                // There is no sending event currently.
                // Pop the context to set a sending event.
                ScheduleEvent();
            }
        }
        else if (m_sbf_enabled)
        {
            m_queue_size += sdnKvsHeader.GetKsSiz();
            // Build schedule context
            ScheduleContext ctx =
                {Simulator::Now(), sdnKvsHeader, socket, packet, from, ServerSchedulingType::SBF};
            // Push the context into the queue
            m_queue.push(ctx);
            if (!m_cur_event.IsRunning())
            {
                // There is no sending event currently.
                // Pop the context to set a sending event.
                ScheduleEvent();
            }
        }
        else
        {
            HandlePacket(packet, socket, from);
            // Schedule send packet event
            Time dt = Seconds(1.0 * m_queue_size / (1000.0 * m_server_rate));
            std::cout << "[Server][" << Simulator::Now().As(Time::S) << "] Server " << m_server_id
                      << "(queue_siz=" << m_queue_size
                      << ", update_time=" << MilliSeconds(m_lst).As(Time::S)
                      << ") will finish handled in " << dt.As(Time::S) << std::endl;
            Simulator::Schedule(dt, &Server::SendPacket, this, socket, packet, from);
        }
        // MODIFY END
    }
}

void
Server::HandlePacket(Ptr<Packet> packet, Ptr<Socket> socket, Address from)
{
    // Deserialize SDN-KVS header
    SdnKvsHeader sdnKvsHeader;
    packet->RemoveHeader(sdnKvsHeader);

    // Update queue size
    int64_t cur = Simulator::Now().GetMilliSeconds();
    if (m_lst > 0)
    {
        m_queue_size -= std::min(m_queue_size, uint32_t(m_server_rate * (cur - m_lst)));
        m_queue_out += std::min(m_queue_size, uint32_t(m_server_rate * (cur - m_lst)));
    }

    m_queue_size += sdnKvsHeader.GetKsSiz();

    m_queue_size_avg = 0.99 * m_queue_size_avg + 0.01 * m_queue_size;    // 均值队列
    m_queue_size_max = std::max(m_queue_size_max, (double)m_queue_size); // 最大队列

    // Check and set heavy flag.
    // Default value is 0, so just check the condition of being greater than TQS.
    sdnKvsHeader.SetHeavy(0U);

    if (cur - m_maxq_lst > deltaT)
    {
        if (m_queue_size + m_queue_out > m_queue_size_last)
            m_util_avg_short = 1.0 * (m_queue_size + m_queue_out - m_queue_size_last) /
                               (m_server_rate * (cur - m_maxq_lst));
        else
            m_util_avg_short = 0;

        m_util_avg_long = 0.99 * m_util_avg_long + 0.01 * m_util_avg_short;

        std::cout << "[Debug] " << m_server_id << "( que_avg = " << m_queue_size_avg
                  << ", que_max = " << m_queue_size_max << ", tqs = " << m_tqs_dynamic
                  << " que_siz = " << m_queue_size << ", que_size_last = " << m_queue_size_last
                  << ", que_out = " << m_queue_out << ", deltaT = " << cur - m_maxq_lst
                  << ", u_long = " << m_util_avg_long << ", u_short = " << m_util_avg_short
                  << ", server_rate = " << m_server_rate << ", SR_avg = " << m_SR_avg << ")"
                  << std::endl;

        m_queue_size_last = m_queue_size;
        m_queue_out = 0;
        m_queue_size_max = m_queue_size_avg;
        m_maxq_lst = cur;
    }

    m_lst = m_queue_size ? cur : 0;

    m_tqs_dynamic = m_queue_size_avg * m_queue_size_avg / m_queue_size_max *
                    (m_server_rate / m_SR_avg) *
                    (m_util_avg_long / std::max(m_util_avg_short, 0.1));

    if (m_tqs_dynamic < 2.0)
        m_tqs_dynamic = 2.0;
    // if (m_tqs_dynamic > m_queue_size_avg * m_queue_size_avg / m_queue_size_max)
    //     m_tqs_dynamic = m_queue_size_avg * m_queue_size_avg / m_queue_size_max;

    m_lst = cur;
    sdnKvsHeader.SetHeavy(0); // Set heavy flag when sending packet

    if (f_tqs_dynamic)
    {
        if (m_sdn_kvs_enabled && m_queue_size >= m_tqs_dynamic)
        {
            Ptr<Packet> pkt = Create<Packet>();
            SdnKvsHeader hdr;
            hdr.SetId(0);
            hdr.SetReqId(sdnKvsHeader.GetReqId());
            hdr.SetKsSiz(sdnKvsHeader.GetKsSiz());
            hdr.SetHeavy(1U);
            hdr.SetQueueSiz(m_queue_size);
            hdr.SetServerRate(m_server_rate);
            hdr.SetServerTqs(m_tqs_dynamic);
            pkt->AddHeader(hdr);
            socket->SendTo(pkt, 0, from);
            // Server overloaded, send signal packet to SDN controller
            std::cout << "[Server][" << Simulator::Now().As(Time::S) << "] Server " << m_server_id
                      << " is overloaded, set heavy field to 1"
                      << "(queue_siz = " << m_queue_size << ", que_avg = " << m_queue_size_avg
                      << ", que_max = " << m_queue_size_max << ", tqs_dynamic = " << m_tqs_dynamic
                      << ", tqs = " << m_tqs << ", util_avg_long = " << m_util_avg_long
                      << ", util_avg_short = " << m_util_avg_short << ")" << std::endl;
        }
    }
    else if (m_sdn_kvs_enabled && m_queue_size >= m_tqs)
    {
        Ptr<Packet> pkt = Create<Packet>();
        SdnKvsHeader hdr;
        hdr.SetId(0);
        hdr.SetReqId(sdnKvsHeader.GetReqId());
        hdr.SetKsSiz(sdnKvsHeader.GetKsSiz());
        hdr.SetHeavy(1U);
        hdr.SetQueueSiz(m_queue_size);
        hdr.SetServerRate(m_server_rate);
        hdr.SetServerTqs(m_tqs);
        pkt->AddHeader(hdr);
        socket->SendTo(pkt, 0, from);
    }
    sdnKvsHeader.SetQueueSiz(m_queue_size);
    sdnKvsHeader.SetServerRate(m_server_rate);
    if (f_tqs_dynamic)
        sdnKvsHeader.SetServerTqs(m_tqs_dynamic);
    else
        sdnKvsHeader.SetServerTqs(m_tqs);

    packet->AddHeader(sdnKvsHeader);
}

void
Server::ScheduleEvent()
{
    if (!m_queue.empty())
    {
        ScheduleContext ctx = m_queue.top();
        m_queue.pop();
        // Print log
        std::stringstream ss;
        ctx.sdnKvsHeader.Print(ss);
        // Update the queue size
        uint32_t ks_siz = ctx.sdnKvsHeader.GetKsSiz();
        // Even though it is not sent yet, we do not count the keyset size for it
        m_queue_size -= ks_siz;
        // Schedule send packet event
        Time dt = Seconds(1.0 * ks_siz / (1000.0 * m_server_rate));
        std::cout << "[Server][" << Simulator::Now().As(Time::S) << "] Server " << m_server_id
                  << "(queue_siz=" << m_queue_size
                  << ", update_time=" << MilliSeconds(m_lst).As(Time::S)
                  << ") begin to handle packet" << ss.str() << ", it will finish handled in "
                  << dt.As(Time::S) << std::endl;
        std::cout << "[Server][" << Simulator::Now().As(Time::S) << "] Server " << m_server_id
                  << "(queue_siz=" << m_queue_size
                  << ", update_time=" << MilliSeconds(m_lst).As(Time::S)
                  << ") will finish handled in " << dt.As(Time::S) << std::endl;
        m_cur_event =
            Simulator::Schedule(dt, &Server::SendPacket, this, ctx.socket, ctx.packet, ctx.from);
    }
}

void
Server::SendPacket(Ptr<Socket> socket, Ptr<Packet> packet, Address& from)
{
    SdnKvsHeader sdnKvsHeader;
    packet->RemoveHeader(sdnKvsHeader);
    if (!m_srpt_enabled && !m_sbf_enabled && m_lst > 0)
    {
        // Update queue size if the queue is not empty(and SRPT/sbf algorithm is disabled)
        int64_t cur = Simulator::Now().GetMilliSeconds();
        m_queue_size -= std::min(m_queue_size, uint32_t(m_server_rate * (cur - m_lst)));
        m_queue_out += std::min(m_queue_size, uint32_t(m_server_rate * (cur - m_lst)));

        m_lst = m_queue_size ? cur : 0;
        // Check and set heavy flag.
        // Default value is 0, so just check the condition of being greater than TQS.
        if (f_tqs_dynamic)
        {
            if (m_queue_size >= m_tqs_dynamic)
            {
                // Server overloaded, send signal packet to SDN controller
                std::cout << "[Server][" << Simulator::Now().As(Time::S) << "] Server "
                          << m_server_id << " is overloaded, set heavy field to 1"
                          << "(queue_siz = " << m_queue_size << ", que_avg = " << m_queue_size_avg
                          << ", que_max = " << m_queue_size_max
                          << ", tqs_dynamic = " << m_tqs_dynamic << ", tqs = " << m_tqs
                          << ", util_avg_long = " << m_util_avg_long
                          << ", util_avg_short = " << m_util_avg_short << ")" << std::endl;
                sdnKvsHeader.SetHeavy(1U);
            }
        }
        else if (m_queue_size > m_tqs)
        {
            // Server overloaded, send signal packet to SDN controller
            std::cout << "[Server][" << Simulator::Now().As(Time::S) << "] Server " << m_server_id
                      << " is overloaded, set heavy field to 1"
                      << "(queue_siz = " << m_queue_size << ", que_avg = " << m_queue_size_avg
                      << ", tqs = " << m_tqs << ")" << std::endl;
            sdnKvsHeader.SetHeavy(1U);
        }
    }

    sdnKvsHeader.SetQueueSiz(m_queue_size);
    sdnKvsHeader.SetServerRate(m_server_rate);
    if (f_tqs_dynamic)
        sdnKvsHeader.SetServerTqs(m_tqs_dynamic);
    else
        sdnKvsHeader.SetServerTqs(m_tqs);
    packet->AddHeader(sdnKvsHeader);
    std::stringstream ss;
    sdnKvsHeader.Print(ss);

    std::cout << "[Server][" << Simulator::Now().As(Time::S) << "] Server " << m_server_id
              << "(queue_siz=" << m_queue_size
              << ", update_time=" << MilliSeconds(m_lst).As(Time::S)
              << ") finishes handling the packet" << ss.str() << std::endl;

    socket->SendTo(packet, 0, from);
    if (InetSocketAddress::IsMatchingType(from))
    {
        std::cout << "[Server][" << Simulator::Now().As(Time::S) << "] Server " << m_server_id
                  << "(queue_siz=" << m_queue_size
                  << ", update_time=" << MilliSeconds(m_lst).As(Time::S) << ") sent response packet"
                  << ss.str() << " to " << InetSocketAddress::ConvertFrom(from).GetIpv4()
                  << " port " << InetSocketAddress::ConvertFrom(from).GetPort() << std::endl;
    }
    else if (Inet6SocketAddress::IsMatchingType(from))
    {
        std::cout << "[Server][" << Simulator::Now().As(Time::S) << "] Server " << m_server_id
                  << "(queue_siz=" << m_queue_size
                  << ", update_time=" << MilliSeconds(m_lst).As(Time::S) << ") sent response packet"
                  << ss.str() << " to " << Inet6SocketAddress::ConvertFrom(from).GetIpv6()
                  << " port " << Inet6SocketAddress::ConvertFrom(from).GetPort() << std::endl;
    }

    // Current sending event has been expired.
    // Set a new sending event if the priority queue is not empty.
    if (m_srpt_enabled || m_sbf_enabled)
    {
        ScheduleEvent();
    }
}
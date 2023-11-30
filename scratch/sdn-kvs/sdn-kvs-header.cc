#include "sdn-kvs-header.h"

SdnKvsHeader::SdnKvsHeader()
{
}

SdnKvsHeader::~SdnKvsHeader()
{
}

void
SdnKvsHeader::SetReqId(uint32_t req_id)
{
    m_req_id = req_id;
}

uint32_t
SdnKvsHeader::GetReqId() const
{
    return m_req_id;
}

void
SdnKvsHeader::SetKsSiz(uint32_t ks_siz)
{
    m_ks_siz = ks_siz;
}

uint32_t
SdnKvsHeader::GetKsSiz() const
{
    return m_ks_siz;
}

void
SdnKvsHeader::SetBottleneckKsSiz(uint32_t bottleneck_ks_siz)
{
    m_bottleneck_ks_siz = bottleneck_ks_siz;
}

uint32_t
SdnKvsHeader::GetBottleneckKsSiz() const
{
    return m_bottleneck_ks_siz;
}

void
SdnKvsHeader::SetQueueSiz(uint32_t queue_siz)
{
    m_queue_siz = queue_siz;
}

uint32_t
SdnKvsHeader::GetQueueSiz() const
{
    return m_queue_siz;
}

void
SdnKvsHeader::SetServerRate(uint32_t server_rate)
{
    m_server_rate = server_rate;
}

uint32_t
SdnKvsHeader::GetServerRate() const
{
    return m_server_rate;
}

void
SdnKvsHeader::SetHeavy(uint16_t heavy)
{
    m_heavy = heavy;
}

uint16_t
SdnKvsHeader::GetHeavy() const
{
    return m_heavy;
}

void
SdnKvsHeader::SetPriority(uint32_t priority)
{
    m_priority = priority;
}

uint32_t
SdnKvsHeader::GetPriority() const
{
    return m_priority;
}

TypeId
SdnKvsHeader::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::SdnKvsHeader").SetParent<Header>().AddConstructor<SdnKvsHeader>();
    return tid;
}

TypeId
SdnKvsHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

void
SdnKvsHeader::Print(std::ostream& os) const
{
    os << "(req_id=" << m_req_id << ", ks_siz=" << m_ks_siz
       << ", bottleneck_ks_siz=" << m_bottleneck_ks_siz << ", queue_siz=" << m_queue_siz
       << ", server_rate=" << m_server_rate << ", heavy=" << m_heavy << ", priority=" << m_priority
       << ")";
}

uint32_t
SdnKvsHeader::GetSerializedSize() const
{
    return 26U; // 26 bytes
}

void
SdnKvsHeader::Serialize(Buffer::Iterator start) const
{
    // Write in network byte order.
    start.WriteHtonU32(m_req_id);
    start.WriteHtonU32(m_ks_siz);
    start.WriteHtonU32(m_bottleneck_ks_siz);
    start.WriteHtonU32(m_queue_siz);
    start.WriteHtonU32(m_server_rate);
    start.WriteHtonU16(m_heavy);
    start.WriteHtonU32(m_priority);
}

uint32_t
SdnKvsHeader::Deserialize(Buffer::Iterator start)
{
    // Read in network byte order and store in host byte order.
    m_req_id = start.ReadNtohU32();
    m_ks_siz = start.ReadNtohU32();
    m_bottleneck_ks_siz = start.ReadNtohU32();
    m_queue_siz = start.ReadNtohU32();
    m_server_rate = start.ReadNtohU32();
    m_heavy = start.ReadNtohU16();
    m_priority = start.ReadNtohU32();
    return 26U;
}
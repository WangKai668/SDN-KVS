#include "server-helper.h"

#include <ns3/boolean.h>
#include <ns3/uinteger.h>

ServerHelper::ServerHelper()
{
    m_factory.SetTypeId(Server::GetTypeId());
}

ServerHelper::ServerHelper(uint16_t port)
{
    m_factory.SetTypeId(Server::GetTypeId());
    SetAttribute("Port", UintegerValue(port));
}

ServerHelper::ServerHelper(uint16_t port,
                           ServerRateRatio ratio,
                           uint32_t tqs,
                           SchedulingType algorithm)
{
    m_factory.SetTypeId(Server::GetTypeId());
    m_server_rate_ratio = ratio;
    SetAttribute("Port", UintegerValue(port));
    SetAttribute("TQS", UintegerValue(tqs));
    SetAttribute("SrptEnabled",
                 BooleanValue(algorithm.server_algorithm == ServerSchedulingType::SRPT));
    SetAttribute("SbfEnabled",
                 BooleanValue(algorithm.server_algorithm == ServerSchedulingType::SBF));
}

void
ServerHelper::SetAttribute(std::string name, const AttributeValue& value)
{
    m_factory.Set(name, value);
}

ApplicationContainer
ServerHelper::Install(NodeContainer c)
{
    ApplicationContainer apps;
    uint32_t cnt = 0;
    for (NodeContainer::Iterator i = c.Begin(); i != c.End(); ++i)
    {
        Ptr<Node> node = *i;
        m_server = m_factory.Create<Server>();
        m_server->SetAttribute("ServerId", UintegerValue(cnt++));
        node->AddApplication(m_server);
        apps.Add(m_server);
    }
    uint32_t ratio_tot = m_server_rate_ratio.r1 + m_server_rate_ratio.r2 + m_server_rate_ratio.r3;
    uint32_t cnt1 = cnt * m_server_rate_ratio.r1 / ratio_tot;
    uint32_t cnt2 = cnt * m_server_rate_ratio.r2 / ratio_tot;
    uint32_t cnt3 = cnt * m_server_rate_ratio.r3 / ratio_tot;
    uint32_t ptr = 0;
    while (ptr < cnt1)
        apps.Get(ptr++)->SetAttribute("ServerRate", UintegerValue(1U));
    while (ptr < cnt1 + cnt2)
        apps.Get(ptr++)->SetAttribute("ServerRate", UintegerValue(2U));
    while (ptr < cnt1 + cnt2 + cnt3)
        apps.Get(ptr++)->SetAttribute("ServerRate", UintegerValue(3U));
    return apps;
}

Ptr<Server>
ServerHelper::GetServer()
{
    return m_server;
}
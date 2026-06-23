#include "server-helper.h"

#include <ns3/boolean.h>
#include <ns3/double.h>
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
                           SchedulingType algorithm,
                           uint32_t f_tqs_dynamic,
                           double SR_avg)
{
    m_factory.SetTypeId(Server::GetTypeId());
    m_server_rate_ratio = ratio;
    m_server_rate_avg = SR_avg;
    SetAttribute("Port", UintegerValue(port));
    SetAttribute("TQS", UintegerValue(tqs));
    SetAttribute("SrptEnabled",
                 BooleanValue(algorithm.server_algorithm == ServerSchedulingType::SRPT));
    SetAttribute("SbfEnabled",
                 BooleanValue(algorithm.server_algorithm == ServerSchedulingType::SBF));
    SetAttribute("SdnKvsEnabled",
                 BooleanValue(algorithm.server_algorithm == ServerSchedulingType::SDN_KVS));
    SetAttribute("F_tqs_dynamic", UintegerValue(f_tqs_dynamic));
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
    std::vector<uint32_t> ids;
    uint32_t cnt = 0;
    for (NodeContainer::Iterator i = c.Begin(); i != c.End(); ++i)
    {
        Ptr<Node> node = *i;
        ids.push_back(cnt);
        m_server = m_factory.Create<Server>();
        m_server->SetAttribute("ServerId", UintegerValue(cnt++));
        node->AddApplication(m_server);
        apps.Add(m_server);
    }
    std::random_shuffle(ids.begin(), ids.end());
    uint32_t ratio_tot = m_server_rate_ratio.r1 + m_server_rate_ratio.r2 + m_server_rate_ratio.r3;
    uint32_t cnt1 = cnt * m_server_rate_ratio.r1 / ratio_tot;
    uint32_t cnt2 = cnt * m_server_rate_ratio.r2 / ratio_tot;
    for (uint32_t i = 0; i < ids.size(); i++)
    {
        uint32_t server_rate = (i < cnt1) ? 1 : ((i < cnt1 + cnt2) ? 2 : 3);
        apps.Get(ids[i])->SetAttribute("ServerRate",
                                       UintegerValue(server_rate)); // modify by wk
        apps.Get(ids[i])->SetAttribute("SRavg", DoubleValue(m_server_rate_avg));
    }
    return apps;
}

Ptr<Server>
ServerHelper::GetServer()
{
    return m_server;
}
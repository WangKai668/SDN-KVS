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

ServerHelper::ServerHelper(uint16_t port, uint32_t server_rate, uint32_t tqs, bool enable_srpt)
{
    m_factory.SetTypeId(Server::GetTypeId());
    SetAttribute("Port", UintegerValue(port));
    SetAttribute("ServerRate", UintegerValue(server_rate));
    SetAttribute("TQS", UintegerValue(tqs));
    SetAttribute("SrptEnabled", BooleanValue(enable_srpt));
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
        m_server->SetServerId(cnt++);
        node->AddApplication(m_server);
        apps.Add(m_server);
    }
    return apps;
}

Ptr<Server>
ServerHelper::GetServer()
{
    return m_server;
}
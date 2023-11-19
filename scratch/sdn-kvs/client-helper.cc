#include "client-helper.h"

#include <ns3/boolean.h>
#include <ns3/uinteger.h>

ClientHelper::ClientHelper()
{
    m_factory.SetTypeId(Client::GetTypeId());
}

ClientHelper::ClientHelper(std::map<uint32_t, std::vector<RequestTableEntry>>& req_table,
                           bool enable_sbf)
{
    m_factory.SetTypeId(Client::GetTypeId());
    m_req_table = req_table;
    SetAttribute("SbfEnabled", BooleanValue(enable_sbf));
}

void
ClientHelper::SetAttribute(std::string name, const AttributeValue& value)
{
    m_factory.Set(name, value);
}

ApplicationContainer
ClientHelper::Install(NodeContainer c)
{
    ApplicationContainer apps;
    for (auto& [client_id, request_list] : m_req_table)
    {
        m_client = m_factory.Create<Client>();
        m_client->SetRequestList(request_list);
        Ptr<Node> node = c.Get(client_id);
        node->AddApplication(m_client);
        apps.Add(m_client);
    }
    return apps;
}

Ptr<Client>
ClientHelper::GetClient()
{
    return m_client;
}
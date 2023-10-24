#ifndef CLIENT_HELPER_H
#define CLIENT_HELPER_H

#include "client.h"

#include <ns3/application-container.h>
#include <ns3/node-container.h>
#include <ns3/object-factory.h>

using namespace ns3;

/**
 * \brief Create a client application which sends SDN-KVS packets.
 */
class ClientHelper
{
  public:
    /**
     * Create ClientHelper which will make life easier for people trying
     * to set up simulations with SDN-KVS client application.
     *
     */
    ClientHelper();

    /**
     * Create ClientHelper which will make life easier for people trying
     * to set up simulations with SDN-KVS client application.
     *
     * \param req_table Request table
     */
    ClientHelper(std::map<uint32_t, std::vector<RequestTableEntry>>& req_table);

    /**
     * Record an attribute to be set in each Application after it is is created.
     *
     * \param name the name of the attribute to set
     * \param value the value of the attribute to set
     */
    void SetAttribute(std::string name, const AttributeValue& value);

    /**
     * Create one SDN-KVS client application on each of the Nodes in the
     * NodeContainer.
     *
     * \param c The nodes on which to create the Applications.  The nodes
     *          are specified by a NodeContainer.
     * \returns The applications created, one Application per Node in the
     *          NodeContainer.
     */
    ApplicationContainer Install(NodeContainer c);

    /**
     * \brief Return the last created client.
     *
     * This function is mainly used for testing.
     *
     * \returns a Ptr to the last created client application
     */
    Ptr<Client> GetClient();

  private:
    ObjectFactory m_factory; //!< Object factory.
    Ptr<Client> m_client;    //!< The last created client application
    std::map<uint32_t, std::vector<RequestTableEntry>> m_req_table; //!< Request table
};

#endif
#ifndef SERVER_HELPER_H
#define SERVER_HELPER_H

#include "server.h"

#include <ns3/application-container.h>
#include <ns3/node-container.h>
#include <ns3/object-factory.h>

using namespace ns3;

/**
 * \brief Create a server application which waits for input SDN-KVS packets.
 */
class ServerHelper
{
  public:
    /**
     * Create ServerHelper which will make life easier for people trying
     * to set up simulations with SDN-KVS server application.
     *
     */
    ServerHelper();

    /**
     * Create ServerHelper which will make life easier for people trying
     * to set up simulations with SDN-KVS server application.
     *
     * \param port The port the server will wait on for incoming packets
     */
    ServerHelper(uint16_t port);

    /**
     * Create ServerHelper which will make life easier for people trying
     * to set up simulations with SDN-KVS server application.
     *
     * \param port The port the server will wait on for incoming packets
     * \param ratio Server rate ratio
     * \param tqs Server TQS
     * \param algorithm The scheduling algorithm running on the server
     */
    ServerHelper(uint16_t port,
                 ServerRateRatio ratio,
                 uint32_t tqs,
                 SchedulingType algorithm,
                 uint32_t f_tqs_dynamic,
                 double SR_avg);

    /**
     * Record an attribute to be set in each Application after it is is created.
     *
     * \param name the name of the attribute to set
     * \param value the value of the attribute to set
     */
    void SetAttribute(std::string name, const AttributeValue& value);

    /**
     * Create one SDN-KVS server application on each of the Nodes in the
     * NodeContainer.
     *
     * \param c The nodes on which to create the Applications.  The nodes
     *          are specified by a NodeContainer.
     * \returns The applications created, one Application per Node in the
     *          NodeContainer.
     */
    ApplicationContainer Install(NodeContainer c);

    /**
     * \brief Return the last created server.
     *
     * This function is mainly used for testing.
     *
     * \returns a Ptr to the last created server application
     */
    Ptr<Server> GetServer();

  private:
    ObjectFactory m_factory;                         //!< Object factory.
    Ptr<Server> m_server;                            //!< The last created server application
    ServerRateRatio m_server_rate_ratio = {1, 0, 0}; //!< Server rate ratio
    double m_server_rate_avg;
};

#endif
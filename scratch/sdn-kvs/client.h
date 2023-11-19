#ifndef CLIENT_H
#define CLIENT_H

#include "model.h"

#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ipv4-address.h"
#include "ns3/ptr.h"
#include "ns3/socket.h"
#include "ns3/traced-callback.h"

using namespace ns3;

/**
 * \brief A SDN-KVS client
 *
 * Every packet sent should be returned by the server and received here.
 */
class Client : public Application
{
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    Client();

    ~Client() override;

    /**
     * Set request list for client
     */
    void SetRequestList(std::vector<RequestTableEntry>& request_list);

  protected:
    void DoDispose() override;

  private:
    void StartApplication() override;
    void StopApplication() override;

    /**
     * \brief Send a packet
     * \param dst_ip Destination server IPv4 address string
     * \param dst_port Destination server listen port
     */
    void Send(Ptr<Packet> packet, std::string dst_ip, uint16_t dst_port);

    /**
     * \brief Create a socket that connected to the server.
     * \param peerAddress Server IP address
     * \param peerPort Server listen port
     * \return Socket connected to the server
     */
    Ptr<Socket> Connect(Address peerAddress, uint16_t peerPort);

    /**
     * \brief Handle a packet reception.
     *
     * This function is called by lower layers.
     *
     * \param socket the socket the packet was received to.
     */
    void HandleRead(Ptr<Socket> socket);

    /**
     * \brief Schedule the next packet transmission
     */
    void ScheduleTransmit();

    std::vector<RequestTableEntry> m_request_list; //!< Request list for this client
    bool m_sbf_enabled;                            //!< Request list for this client
    uint32_t m_cur = 0;                            //!< Next request list index to be schedule

    /// Callbacks for tracing the packet Tx events
    TracedCallback<Ptr<const Packet>> m_txTrace;

    /// Callbacks for tracing the packet Rx events
    TracedCallback<Ptr<const Packet>> m_rxTrace;

    /// Callbacks for tracing the packet Tx events, includes source and destination addresses
    TracedCallback<Ptr<const Packet>, const Address&, const Address&> m_txTraceWithAddresses;

    /// Callbacks for tracing the packet Rx events, includes source and destination addresses
    TracedCallback<Ptr<const Packet>, const Address&, const Address&> m_rxTraceWithAddresses;
};

#endif

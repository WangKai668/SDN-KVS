#ifndef SERVER_H
#define SERVER_H

#include "ns3/address.h"
#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/socket.h"
#include "ns3/traced-callback.h"

using namespace ns3;

/**
 * \brief A SDN-KVS server
 *
 * Every packet received is sent back.
 */
class Server : public Application
{
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();
    Server();
    ~Server() override;

    uint32_t GetServerId();
    
    void SetServerId(uint32_t server_id);

  protected:
    void DoDispose() override;

  private:
    void StartApplication() override;
    void StopApplication() override;

    /**
     * \brief Handle a packet reception.
     *
     * This function is called by lower layers.
     *
     * \param socket the socket the packet was received to.
     */
    void HandleRead(Ptr<Socket> socket);

    /**
     * Do SDN-KVS handle logic (check queue size and modify the packet header, etc.)
     *
     * \param packet Packet received from client.
     * \param from Where the packet is from.
     * \return Return keyset size of this packet.
     */
    uint32_t HandlePacket(Ptr<Packet> packet);

    /**
     * Send packet to client.
     *
     * \param socket The socket the packet was received to.
     * \param packet Packet waiting to be sent.
     * \param from Where the packet is from.
     * \param ks_siz Keyset size of this packet.
     */
    void SendPacket(Ptr<Socket> socket, Ptr<Packet> packet, Address& from, uint32_t ks_siz);

    uint32_t m_server_id;  //!< Server ID
    uint16_t m_port;       //!< Port on which we listen for incoming packets.
    Ptr<Socket> m_socket;  //!< IPv4 Socket
    Ptr<Socket> m_socket6; //!< IPv6 Socket
    Address m_local;       //!< local multicast address

    uint32_t m_server_rate; //!< Server handle rate. (Number of keys processed per millisecond)
    uint32_t m_queue_size;  //!< Current server queue size
    uint32_t m_tqs;         //!< Server TQS
    int64_t m_lst;          //!< Last time to update queue size

    /// Callbacks for tracing the packet Rx events
    TracedCallback<Ptr<const Packet>> m_rxTrace;

    /// Callbacks for tracing the packet Rx events, includes source and destination addresses
    TracedCallback<Ptr<const Packet>, const Address&, const Address&> m_rxTraceWithAddresses;
};

#endif

#ifndef SERVER_H
#define SERVER_H

#include "model.h"
#include "sdn-kvs-header.h"

#include "ns3/address.h"
#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/socket.h"
#include "ns3/traced-callback.h"

#include <queue>

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
    /**
     * If SRPT/SBF algorithm is enabled on the server, this context is saved in the queue
     * to be scheduled some time.
     */
    struct ScheduleContext
    {
        SdnKvsHeader sdnKvsHeader; // Avoid deserialize repeatedlly
        Ptr<Socket> socket;
        Ptr<Packet> packet;
        Address from;
        ServerSchedulingType algorithm;
    };

    struct Compare
    {
        bool operator()(const ScheduleContext& ctx1, const ScheduleContext& ctx2)
        {
            switch (ctx1.algorithm)
            {
            // used by SBF algorithm
            case ServerSchedulingType::SBF:
                return ctx1.sdnKvsHeader.GetBottleneckKsSiz() <
                       ctx2.sdnKvsHeader.GetBottleneckKsSiz();
                break;
            default:
            // used by SRPT algorithm
            case ServerSchedulingType::SRPT:
                return ctx1.sdnKvsHeader.GetKsSiz() < ctx2.sdnKvsHeader.GetKsSiz();
            }
        };
    };

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
     */
    void HandlePacket(Ptr<Packet> packet);

    /**
     * Pop a packet from the priority queue to set a new sending event.
     */
    void ScheduleEvent();

    /**
     * Send packet to client.
     *
     * \param socket The socket the packet was received to.
     * \param packet Packet waiting to be sent.
     * \param from Where the packet is from.
     * \param ks_siz Keyset size of this packet.
     */
    void SendPacket(Ptr<Socket> socket, Ptr<Packet> packet, Address& from);

    uint16_t m_port;       //!< Port on which we listen for incoming packets.
    Ptr<Socket> m_socket;  //!< IPv4 Socket
    Ptr<Socket> m_socket6; //!< IPv6 Socket
    Address m_local;       //!< local multicast address

    uint32_t m_server_id;   //!< Server ID
    uint32_t m_server_rate; //!< Server handle rate. (Number of keys processed per millisecond)
    uint32_t m_queue_size;  //!< Current server queue size
    uint32_t m_tqs;         //!< Server TQS
    int64_t m_lst;          //!< Last time to update queue size

    bool m_srpt_enabled; //!< Whether SRPT algorithm is enabled on the server
    bool m_sbf_enabled;  //!< Whether SBF algorithm is enabled on the server
    EventId m_cur_event; //!< Current sending event

    /**
     * Compare function for priority queue
     */

    //!< Priority queue used by SRPT/SBF algorithm
    std::priority_queue<ScheduleContext, std::vector<ScheduleContext>, Compare> m_queue;

    /// Callbacks for tracing the packet Rx events
    TracedCallback<Ptr<const Packet>> m_rxTrace;

    /// Callbacks for tracing the packet Rx events, includes source and destination addresses
    TracedCallback<Ptr<const Packet>, const Address&, const Address&> m_rxTraceWithAddresses;
};

#endif

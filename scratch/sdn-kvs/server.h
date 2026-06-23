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

  protected:
    void DoDispose() override;

  private:
    /**
     * If SRPT/SBF algorithm is enabled on the server, this context is saved in the queue
     * to be scheduled some time.
     */
    struct ScheduleContext
    {
        Time arrive_time;
        SdnKvsHeader sdnKvsHeader; // Avoid deserialize repeatedlly
        Ptr<Socket> socket;
        Ptr<Packet> packet;
        Address from;
        ServerSchedulingType algorithm;
    };

    /**
     * Compare function for priority queue
     */
    struct Compare
    {
        bool operator()(const ScheduleContext& ctx1, const ScheduleContext& ctx2)
        {
            switch (ctx1.algorithm)
            {
            // used by SBF algorithm
            case ServerSchedulingType::SBF: {
                if (ctx1.sdnKvsHeader.GetBottleneckKsSiz() ==
                    ctx2.sdnKvsHeader.GetBottleneckKsSiz())
                    return ctx1.arrive_time.GetMilliSeconds() > ctx2.arrive_time.GetMilliSeconds();
                return ctx1.sdnKvsHeader.GetBottleneckKsSiz() >
                       ctx2.sdnKvsHeader.GetBottleneckKsSiz();
            }
            default:
            // used by SRPT algorithm
            case ServerSchedulingType::SRPT: {
                if (ctx1.sdnKvsHeader.GetPriority() == ctx2.sdnKvsHeader.GetPriority())
                    return ctx1.arrive_time.GetMilliSeconds() > ctx2.arrive_time.GetMilliSeconds();
                return ctx1.sdnKvsHeader.GetPriority() > ctx2.sdnKvsHeader.GetPriority();
            }
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
    void HandlePacket(Ptr<Packet> packet, Ptr<Socket> socket, Address from);

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
    double m_tqs_dynamic = 6.0;
    double m_queue_size_avg = 0;
    double m_queue_size_max = 1;
    uint32_t f_tqs_dynamic; // 是否启用动态TQs
    double m_SR_avg;
    double m_util_avg_long = 0.1;
    double m_util_avg_short = 0.1;
    uint32_t m_queue_size_last = 0;
    uint32_t m_queue_out = 0;
    uint32_t deltaT = 40;

    int64_t m_lst; //!< Last time to update queue size
    int64_t m_maxq_lst;

    bool m_srpt_enabled;    //!< Whether SRPT algorithm is enabled on the server
    bool m_sbf_enabled;     //!< Whether SBF algorithm is enabled on the server
    bool m_sdn_kvs_enabled; //!< Whether SDN-KVS algorithm is enabled on the server
    EventId m_cur_event;    //!< Current sending event

    //!< Priority queue used by SRPT/SBF algorithm
    std::priority_queue<ScheduleContext, std::vector<ScheduleContext>, Compare> m_queue;

    /// Callbacks for tracing the packet Rx events
    TracedCallback<Ptr<const Packet>> m_rxTrace;

    /// Callbacks for tracing the packet Rx events, includes source and destination addresses
    TracedCallback<Ptr<const Packet>, const Address&, const Address&> m_rxTraceWithAddresses;
};

#endif

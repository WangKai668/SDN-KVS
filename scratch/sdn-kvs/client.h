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
    /**
     * For DAS algorithm
     */
    struct Server
    {
        uint32_t osk; // The number of sending packet to this server
        double server_rate;
        double capacity;
    };

    /**
     * For DAS algorithm
     */
    struct Keyset
    {
        uint32_t dst_server; // Destination server id(start from 0)
        std::string dst_ip;  // Destination server IPv4 address string
        uint16_t dst_port;   // Destination server port
        uint32_t ks_siz;     // Keyset size
        uint32_t bottleneck_ks_size;
        bool is_sent;
    };

    /**
     * For DAS algorithm
     */
    struct Request
    {
        double st;                   // Send time (keysets from the same request are equal)
        uint32_t src_client;         // Source client id(start from 0)
        uint32_t req_id;             // Request id that the packet belongs to
        std::vector<Keyset> keysets; // Keysets of this request

        bool operator<(const Request& req) const
        {
            return req_id < req.req_id;
        }
    };

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
     * DAS Sort algorithm
     */
    void SortRequests();

    /**
     * Prepare ns3 event for adding request into waiting list
     */
    void ScheduleRequestEvent();

    /**
     * Check if all keysets of the request has been sent
     */
    bool IsAllSent(Request request);

    /**
     * Do DAS schedule algorithm(Add request into waiting list, etc.)
     */
    void HandleRequestEvent(Request request, bool is_last);

    /**
     * Send packet to server when DAS enabled
     */
    void SendOne(uint32_t server_id);

    /**
     * Assign priority for a keyset when DAS enabled
     * \return Keyset priority
     */
    uint32_t AssignPriority(Request req);

    /**
     * Get keyset sum for a specific request and output server id.
     */
    uint32_t SumKeyset(uint32_t req_id, uint32_t server_id);

    /**
     * Find bottleneck server
     */
    uint32_t FindBottleneckServer(std::set<Request>& req_set);

    /**
     * \brief Schedule the next packet transmission
     */
    void ScheduleTransmit();

    std::vector<RequestTableEntry> m_request_list; //!< Request list for this client
    bool m_sbf_enabled; //!< Whether SBF algorithm is enabled on the client
    bool m_das_enabled; //!< Whether DAS algorithm is enabled on the client
    uint32_t m_cur = 0; //!< Next request list index to be schedule

    /**
     * Variable used for DAS
     */
    std::vector<Request> m_request_vec;           //!< Requests waiting for sending
    std::map<uint32_t, Server> m_servers;         //!< Server information
    std::map<std::string, uint32_t> m_ipToServer; //!< Map<IPv4 string, Server id>
    std::map<std::pair<uint32_t, uint32_t>, Time>
        m_ksSendTime; //!< Keyset sending time. Map<(req_id, server_id), send time>

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

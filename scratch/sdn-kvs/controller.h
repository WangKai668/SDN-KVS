#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "sdn-kvs-header.h"

#include <ns3/ofswitch13-module.h>

#include <stack>
using namespace ns3;

/**
 * \brief SDN-KVS controller
 */
class Controller : public OFSwitch13Controller
{
  public:
    Controller();           //!< Default constructor.
    ~Controller() override; //!< Dummy destructor.
    void SetSdnKvsEnabled(bool sdn_kvs_enabled);
    /** Destructor implementation */
    void DoDispose() override;

    /**
     * Register this type.
     * \return The object TypeId.
     */
    static TypeId GetTypeId();

    /**
     * Handle a packet in message sent by the switch to this controller.
     * \note Inherited from OFSwitch13Controller.
     * \param msg The OpenFlow received message.
     * \param swtch The remote switch metadata.
     * \param xid The transaction id from the request message.
     * \return 0 if everything's ok, otherwise an error number.
     */
    ofl_err HandlePacketIn(struct ofl_msg_packet_in* msg,
                           Ptr<const RemoteSwitch> swtch,
                           uint32_t xid) override;

    void AddToServerNodeMap(uint32_t server_id, Ptr<Node> server_node);

    void AddToClientNodeMap(uint32_t client_id, Ptr<Node> client_node);

  protected:
    // Inherited from OFSwitch13Controller
    void HandshakeSuccessful(Ptr<const RemoteSwitch> swtch) override;

  private:
    /**
     * Structure of packet waiting for forwarding to the server.
     * It is saved temporarily in the scheduling context.
     */
    struct SchedulePacket
    {
        uint32_t id;                   // Identify the schedule packet
        SdnKvsHeader sdnKvsHeader;     // SDN-KVS header
        struct ofl_msg_packet_in* msg; // The OpenFlow received message
        Ptr<const RemoteSwitch> swtch; // The remote switch metadata
        uint32_t xid;                  // The transaction id from the request message
        EventId eventId;               // Event ID for sending this packet
        Time arriveTime;               // Arrive time of this packet
        uint32_t dstServerId;          // Destination server id
    };

    /**
     * Structure of request waiting for scheduling.
     * It is the minimum unit of scheduling.
     */
    struct ScheduleRequest
    {
        uint32_t req_id; // Request ID
        double weight;   // Sorted by this weight. Computed in scheduling.
    };

    /**
     * Each server has a schedule context.
     */
    struct ScheduleContext
    {
        bool is_heavy;         // Whether this server is overloaded or not
        Time lastScheduleTime; // Last time for scheduling this server, or overloaded time if not
                               // scheduled yet. Set after each scheduling.
        uint32_t queue_size;   // Current server queue size. Computed in each scheduling.
        uint32_t server_rate;  // Server handle rate. (Number of keys processed per millisecond)
        // Waiting for the sending packets list.
        // It means the server is not overloaded now if empty.
        std::vector<SchedulePacket> schedulePackets;
        // Map<switch id, undo flow table modification event id>
        std::map<uint64_t, EventId> undoFlowEvents;
    };

    /**
     * Configure the switch.
     * \param swtch The switch information.
     */
    void ConfigureSwitch(Ptr<const RemoteSwitch> swtch);

    /**
     * Do packet schedule when a client packet is sent to an overloaded server.
     * After scheduling, each packet will have a new forward time to the target server,
     * and the old forward time event will be canceled.
     * \param sdnKvsHeader SDN-KVS header
     * \param msg The OpenFlow received message.
     * \param swtch The remote switch metadata.
     * \param xid The transaction id from the request message.
     */
    void DoSchedule(SdnKvsHeader& sdnKvsHeader,
                    struct ofl_msg_packet_in* msg,
                    Ptr<const RemoteSwitch> swtch,
                    uint32_t xid);

    /**
     * Check the heavy field in the header and modify the flow table when heavy is set to 1.
     * \param sdnKvsHeader SDN-KVS header
     * \param msg The OpenFlow received message.
     * \param swtch The remote switch metadata.
     * \param xid The transaction id from the request message.
     */
    void DoFlowModify(SdnKvsHeader& sdnKvsHeader,
                      struct ofl_msg_packet_in* msg,
                      Ptr<const RemoteSwitch> swtch,
                      uint32_t xid);
    /**
     * Undo the modification of the flow table.
     * \param switch_id Target switch id.
     * \param server_ip Destination server IPv4 address string.
     */
    void UndoFlowModify(uint32_t switch_id, std::string server_ip);

    /**
     * Get Ipv4Address string from ns3 node object.
     * \param node The pointer of node.
     * \return The IPv4 address string.
     */
    std::string GetIpv4AddressFromNode(Ptr<Node> node);

    /**
     * Extract an IPv4 address from packet match.
     * \param oxm_of The OXM_IF_* IPv4 field.
     * \param match The ofl_match structure pointer.
     * \return The IPv4 address.
     */
    Ipv4Address ExtractIpv4Address(uint32_t oxm_of, struct ofl_match* match);

    /**
     * Get swicth port by client id.
     * \param switch_id The switch id. (start from 1)
     * \param client_id The client_id. (start from 0)
     * \return The switch port id(start from 1) that connected to this client node.
     * Return -1 if it is not connected to this switch.
     */
    int32_t GetSwitchPortByClientId(uint64_t switch_id, uint32_t client_id);

    /**
     * Get swicth port by server id.
     * \param switch_id The switch id. (start from 1)
     * \param server_id The server_id. (start from 0)88
     * \return The switch port id(start from 1) that connected to this server node.
     * Return -1 if it is not connected to this switch.
     */
    int32_t GetSwitchPortByServerId(uint64_t switch_id, uint32_t server_id);

    /**
     * Get switch out port by destination ipv4 address.
     * \param switch_id The switch id. (start from 1)
     * \param ipv4 IPv4 address.
     * \return The switch output port.
     */
    int32_t GetSwitchPortByIPv4(uint64_t switch_id, Ipv4Address& ipv4);

    /**
     * Do nothing for packet data and just forward the packet to the destination.
     * \param msg The OpenFlow received message.
     * \param swtch The remote switch metadata.
     * \param xid The transaction id from the request message.
     * \return 0 if everything's ok, otherwise an error number.
     */
    int ForwardPacket(struct ofl_msg_packet_in* msg, Ptr<const RemoteSwitch> swtch, uint32_t xid);

    /**
     * Remove packet from controller and forward it to the server
     * \param pkt Packet received from client.
     */
    void RemovePacketAndForward(SchedulePacket pkt);

    /**
     * Do BP-LPT algorithm. This function will sort the request list.
     * \param scheduleRequests Request list waiting for scheduling.
     * \return Sorted request list
     */
    std::vector<uint32_t> BP_LPT(std::vector<ScheduleRequest>& scheduleRequests);

    /**
     * Get keyset sum for a specific request set and output server id.
     * \param req_set Request id set
     * \param server_id Server id
     * \return Sum of keyset
     */
    uint32_t SumKeyset(std::set<uint32_t>& req_set, uint32_t server_id);

    /**
     * Get keyset sum for a specific request and output server id.
     * \param req_id Request id
     * \param server_id Server id
     * \return Sum of keyset
     */
    uint32_t SumKeyset(uint32_t req_id, uint32_t server_id);

    /**
     * Find bottleneck server.
     * \param req_set Request id set
     * \return Bottleneck server id
     */
    uint32_t FindBottleneckServer(std::set<uint32_t>& req_set);

    /**
     * Controller has the whole topology of network.
     */
    bool m_sdn_kvs_enabled; //!< Whether sdn-kvs algorithm is enabled on the controller
    std::map<uint32_t, Ptr<Node>> m_serverNodes;  //!< Map<server id, server node>
    std::map<uint32_t, Ptr<Node>> m_clientNodes;  //!< Map<client id, client node>
    std::map<std::string, uint32_t> m_ipToServer; //!< Map<ipv4, server id>
    std::map<std::string, uint32_t> m_ipToClient; //!< Map<ipv4, client id>
    uint32_t m_switchPortCnt[2] = {0, 0};         //!< Switch total port count

    /**
     * These variables are used for scheduling.
     */
    std::map<uint32_t, ScheduleContext> m_scheduleContexts; //!< Map<server id, schedule context>
    uint64_t m_idCnt = 0;                                   //!< For generating schedule packet ID
};

#endif
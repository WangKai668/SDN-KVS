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
    // save
    struct PacketInput
    {
        SdnKvsHeader sdnKvsHeader;
        struct ofl_msg_packet_in* msg;
        Ptr<const RemoteSwitch> swtch;
        uint32_t xid;
        Time arrive_time;
        Address from;
    };

    struct SendEvent
    {
        EventId eventId;
        uint32_t serverId;
    };

    // Inherited from OFSwitch13Controller
    void HandshakeSuccessful(Ptr<const RemoteSwitch> swtch) override;

  private:
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
     * Remove packet from m_vecPacket,and remove corresponding event from eventlist
     * \param msg The OpenFlow received message.
     * \param swtch The remote switch metadata.
     * \param xid The transaction id from the request message.
     */
    void RemovePacketAndForward(SdnKvsHeader& sdnKvsHeader,
                                struct ofl_msg_packet_in* msg,
                                Ptr<const RemoteSwitch> swtch,
                                uint32_t xid);

    /**
     * Controller has the whole topology of network.
     */
    std::map<uint32_t, Ptr<Node>> m_serverNodes;  //!< Map<server id, server node>
    std::map<uint32_t, Ptr<Node>> m_clientNodes;  //!< Map<client id, client node>
    std::map<std::string, uint32_t> m_ipToServer; //!< Map<ipv4, server id>
    std::map<std::string, uint32_t> m_ipToClient; //!< Map<ipv4, client id>
    uint32_t m_switchPortCnt[2] = {0, 0};         //!< Switch total port count

    /**
     * These variables are used for scheduling.
     */
    std::map<uint32_t, uint32_t> m_heavyServer;         //!< Map<server id, packet count>
    std::map<uint32_t, int64_t> m_serverHeavyQueueSize; //!< Map<server id, server heavy queue size>
    std::map<uint32_t, int64_t> m_serverHeavyTime;      //!< Map<server id, server heavy time>
    std::map<uint32_t, uint64_t> m_serverRateList;      //!< Map<server id, server rate>
    std::map<uint32_t, std::vector<PacketInput>> m_vecPacket; //!< Packet in controller for schedule
    std::vector<SendEvent> m_sendEventList;                   //!< Event list
};

#endif
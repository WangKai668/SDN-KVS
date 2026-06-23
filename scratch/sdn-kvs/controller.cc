#include "controller.h"

#include <ns3/core-module.h>
#include <ns3/internet-module.h>
#include <ns3/network-module.h>

#include <cstring>
#include <sstream>

// 定义日志组件和注册Controller类
NS_LOG_COMPONENT_DEFINE("Controller");
NS_OBJECT_ENSURE_REGISTERED(Controller);

// 构造函数
Controller::Controller()
{
}

// 析构函数
Controller::~Controller()
{
}

// 资源释放函数，调用父类的DoDispose
void
Controller::DoDispose()
{
    OFSwitch13Controller::DoDispose();
}

// 设置SDN-KVS功能是否启用
void
Controller::SetSdnKvsEnabled(bool sdn_kvs_enabled)
{
    m_sdn_kvs_enabled = sdn_kvs_enabled;
}

// 设置调试模式
void
Controller::SetDebug(bool debug)
{
    m_debug = debug;
}

// 获取TypeId，注册属性和父类信息
TypeId
Controller::GetTypeId()
{
    static TypeId tid = TypeId("ns3::Controller")
                            .SetParent<OFSwitch13Controller>()
                            .SetGroupName("SDN-KVS")
                            .AddConstructor<Controller>()
                            .AddAttribute("ServerNodeMap",
                                          "Map<server id, server node>. Storing all server nodes.",
                                          ObjectMapValue(),
                                          MakeObjectMapAccessor(&Controller::m_serverNodes),
                                          MakeObjectMapChecker<Node>())
                            .AddAttribute("ClientNodeMap",
                                          "Map<client id, client node>. Storing all client nodes.",
                                          ObjectMapValue(),
                                          MakeObjectMapAccessor(&Controller::m_clientNodes),
                                          MakeObjectMapChecker<Node>())
                            .AddAttribute("m_eta",
                                          "A.",
                                          DoubleValue(2.0),
                                          MakeDoubleAccessor(&Controller::m_eta),
                                          MakeDoubleChecker<double>())
                            .AddAttribute("MinQ",
                                          "A.",
                                          UintegerValue(2),
                                          MakeUintegerAccessor(&Controller::m_minQ),
                                          MakeUintegerChecker<uint32_t>());
    return tid;
}

// 握手成功回调，初始化调度上下文并配置交换机
void
Controller::HandshakeSuccessful(Ptr<const RemoteSwitch> swtch)
{
    if (m_debug)
    {
        std::cout << "[Controller][" << Simulator::Now().As(Time::S) << "] Handshake successfully"
                  << std::endl;
    }
    // 初始化每个服务器的调度上下文
    uint32_t server_num = m_serverNodes.size();
    for (uint32_t i = 0; i < server_num; i++)
    {
        ScheduleContext context;
        context.is_heavy = false;                                // 初始不超载
        context.lastScheduleTime = Simulator::Now();             // 记录当前时间
        context.queue_size = 0;                                  // 队列长度
        context.server_rate = 0;                                 // 服务速率
        context.server_tqs = 0;                                  // 服务阈值
        context.schedulePackets = std::vector<SchedulePacket>(); // 初始化调度包队列
        context.undoFlowEvents[1] = EventId();                   // 撤销事件
        context.undoFlowEvents[2] = EventId();
        m_scheduleContexts[i] = context;
    }
    // 配置交换机流表
    ConfigureSwitch(swtch);
}

// 添加服务器节点到映射表
void
Controller::AddToServerNodeMap(uint32_t server_id, Ptr<Node> server_node)
{
    m_serverNodes.insert(std::pair<uint32_t, Ptr<Node>>(server_id, server_node));
}

// 添加客户端节点到映射表
void
Controller::AddToClientNodeMap(uint32_t client_id, Ptr<Node> client_node)
{
    m_clientNodes.insert(std::pair<uint32_t, Ptr<Node>>(client_id, client_node));
}

// 处理Packet-In消息的主入口
ofl_err
Controller::HandlePacketIn(struct ofl_msg_packet_in* msg,
                           Ptr<const RemoteSwitch> swtch,
                           uint32_t xid)
{
    enum ofp_packet_in_reason reason = msg->reason;

    // 打印收到的Packet-In消息内容
    char* msgStr = ofl_structs_match_to_string((struct ofl_match_header*)msg->match, nullptr);
    std::cout << "[Controller][" << Simulator::Now().As(Time::S)
              << "] Received Packet-In packet: " << msgStr << " from switch " << swtch->GetDpId()
              << std::endl;
    free(msgStr);

    // 只处理因动作触发的Packet-In
    if (reason == OFPR_ACTION)
    {
        Ptr<Packet> packet = Create<Packet>(msg->data, msg->data_length); // Copy buffer to packet
        EthernetHeader ethHeader;
        Ipv4Header ipHeader;
        UdpHeader udpHeader;
        SdnKvsHeader sdnKvsHeader;
        // Parse header
        packet->RemoveHeader(ethHeader);
        packet->RemoveHeader(ipHeader);
        packet->RemoveHeader(udpHeader);
        packet->RemoveHeader(sdnKvsHeader);

        // Get source IP address
        Ipv4Address srcIp = ipHeader.GetSource();
        std::stringstream ss;
        ss << srcIp;
        // If the source IP comes from the client, the controller will schedule.
        // Else do flow table modification.
        // Note: Remember to free the message when everything is ok
        if (m_ipToClient.count(ss.str()))
            DoSchedule(sdnKvsHeader, msg, swtch, xid);
        else if (m_ipToServer.count(ss.str()))
            DoFlowModify(sdnKvsHeader, msg, swtch, xid);
    }
    else
    {
        NS_LOG_WARN("This controller can't handle the packet. Unexpected Packet-In reason.");
        // All handlers must free the message when everything is ok
        ofl_msg_free((struct ofl_msg_header*)msg, nullptr);
    }

    return 0;
}

// 配置交换机流表
void
Controller::ConfigureSwitch(Ptr<const RemoteSwitch> swtch)
{
    // Get the switch datapath ID
    uint64_t swDpId = swtch->GetDpId();

    // Broadcast ARP packet. It is important to add this rule for ARP protocol work well.
    DpctlExecute(swDpId, "flow-mod cmd=add,table=0,prio=100 eth_type=0x806 apply:output=all");
    DpctlExecute(swDpId, "flow-mod cmd=add,table=0,prio=100 eth_type=0x806 apply:output=all");

    // Let's assert the controller know the connection port between switch and client/server node.
    // Currently, the nodes are numbered sequentially starting from the client,
    // with even-numbered nodes connected to switch 1
    // and odd-numbered nodes connected to switch 2.
    // Make switch do routing at L3 network layer. So the match rule should parse the IP packet
    // header. No match rule packet will be dropped.

    // Think about 3 clients(id 0, 1, 2) and 3 servers(id 0, 1, 2),
    // there global id are 0(client), 1(client), 2(client), 3(server), 4(server), 5(server).
    // Switch 1 port 1 is connected to client 0(global id 0)
    // Switch 1 port 2 is connected to client 2(global id 2)
    // Switch 1 port 3 is connected to server 1(global id 4)
    // Switch 2 port 1 is connected to client 1(global id 1)
    // Switch 2 port 2 is connected to server 0(global id 3)
    // Switch 2 port 3 is connected to server 2(global id 5)
    // Swicth 1/2 port 4 is connected to the other switch.

    uint32_t client_num = m_clientNodes.size();
    uint32_t server_num = m_serverNodes.size();
    uint32_t port_cnt = 0; // Count the number of ports on the switch

    // 为每个客户端添加流表项
    for (uint32_t i = 0; i < client_num; i++)
    {
        std::stringstream ss;
        std::string ip_dst = GetIpv4AddressFromNode(m_clientNodes[i]);
        m_ipToClient[ip_dst] = i; // Map IPv4 to client id

        int32_t port = GetSwitchPortByClientId(swDpId, i);
        if (port == -1) // Client node is not connected to this switch
            continue;
        ++port_cnt;
        ss.str(""); // Clear string stream
        ss << "flow-mod cmd=add,table=0,prio=100 "
           << "eth_type=0x0800,ip_dst=" << ip_dst << " apply:nw_dec,output=" << port;
        DpctlExecute(swDpId, ss.str());
    }
    // 为每个服务器添加流表项
    for (uint32_t i = 0; i < server_num; i++)
    {
        std::stringstream ss;
        std::string ip_dst = GetIpv4AddressFromNode(m_serverNodes[i]);
        m_ipToServer[ip_dst] = i; // Map IPv4 to server id
        if (m_sdn_kvs_enabled)
        {
            // Set a flow entry with a higher priority so that
            // all data packets sent from the server node are sent to the controller.
            ss << "flow-mod cmd=add,table=0,prio=200 "
               << "eth_type=0x0800,ip_src=" << ip_dst << " apply:output=ctrl:128";
            DpctlExecute(swDpId, ss.str());
        }

        int32_t port = GetSwitchPortByServerId(swDpId, i);
        if (port == -1) // Server node is not connected to this switch
            continue;
        ++port_cnt;
        ss.str(""); // Clear string stream
        ss << "flow-mod cmd=add,table=0,prio=100 "
           << "eth_type=0x0800,ip_dst=" << ip_dst << " apply:nw_dec,output=" << port;
        DpctlExecute(swDpId, ss.str());
    }
    // 未命中流表的包转发到对端交换机（最后一个端口）
    std::stringstream ss;
    ss << "flow-mod cmd=add,table=0,prio=0 apply:output=" << (++port_cnt);
    DpctlExecute(swDpId, ss.str());

    // 记录该交换机的端口总数
    m_switchPortCnt[swDpId - 1] = port_cnt;

    if (m_debug)
    {
        std::cout << "[Controller][" << Simulator::Now().As(Time::S) << "] Configure switch "
                  << swDpId << " completed" << std::endl;
    }
}

// BP-LPT调度算法实现，返回调度顺序
std::vector<uint32_t>
Controller::BP_LPT(std::vector<ScheduleRequest>& scheduleRequests)
{
    // 统计控制器队列中所有key的数量
    uint32_t ctrl_queue_siz = 0;
    for (const auto& [server_id, ctx] : m_scheduleContexts)
        for (const auto& pkt : ctx.schedulePackets)
            ctrl_queue_siz += pkt.sdnKvsHeader.GetKsSiz();
    // 打印调度请求信息
    uint32_t tot_key_num = 0;
    std::stringstream ss;
    ss << "[";
    for (const auto& req : scheduleRequests)
    {
        ss << "(req_id=" << req.req_id << ", weight=" << req.weight << ", key_num=" << req.key_num
           << ")";
        tot_key_num += req.key_num;
    }
    ss << "]";
    std::cout << "[Controller][" << Simulator::Now().As(Time::S)
              << "] Do BP-LPT for array(size=" << scheduleRequests.size()
              << ", tot_key_num=" << tot_key_num << ", ctrl_queue_siz=" << ctrl_queue_siz
              << "): " << ss.str() << std::endl;

    // 初始化请求集合和权重映射
    std::vector<uint32_t> ret;
    std::set<uint32_t> req_set;
    std::map<uint32_t, double> req_weight;
    for (const auto& req : scheduleRequests)
    {
        req_set.insert(req.req_id);
        req_weight[req.req_id] = req.weight;
    }

    // 打印调试信息
    if (m_debug)
    {
        ss.str("");
        ss << "[";
        for (const auto& [server_id, ctx] : m_scheduleContexts)
            ss << "(server_id=" << server_id << ", " << ctx << ")";
        ss << "]";
        std::cout << "[Controller][" << Simulator::Now().As(Time::S)
                  << "] [Schedule Debug] Server context: " << ss.str() << std::endl;
    }

    uint32_t turn_cnt = 0; // 调度轮数
    // 主循环：每次选择瓶颈服务器，选出最优请求
    while (!req_set.empty())
    {
        // 小根堆，按value升序排列，value=weight/kv
        std::priority_queue<std::pair<double, uint32_t>,
                            std::vector<std::pair<double, uint32_t>>,
                            std::greater<std::pair<double, uint32_t>>>
            q; // <value, req_id>

        // 找到当前瓶颈服务器
        uint32_t sid = FindBottleneckServer(req_set);
        if (m_debug)
        {
            std::cout << "[Controller][" << Simulator::Now().As(Time::S)
                      << "] [Schedule Debug] [Turn " << ++turn_cnt << "] Bottleneck server ID is "
                      << sid << std::endl;
        }
        ss.str("");
        ss << "[";

        // 计算每个请求在该服务器上的value
        for (const auto& req_id : req_set)
        {
            uint32_t kv = SumKeyset(req_id, sid);
            if (kv == 0)
                continue;
            double val = req_weight[req_id] / kv;
            ss << "(req_id=" << req_id << ", weight=" << req_weight[req_id] << ", sum_ks=" << kv
               << ", value=" << val << ")";
            q.push(std::pair<double, uint32_t>(val, req_id));
        }
        // 选出value最小的请求
        uint32_t target = q.top().second;

        ss << "]";
        if (m_debug)
        {
            std::cout << "[Controller][" << Simulator::Now().As(Time::S)
                      << "] [Schedule Debug] [Turn " << turn_cnt << "] Request set: " << ss.str()
                      << std::endl;
            std::cout << "[Controller][" << Simulator::Now().As(Time::S)
                      << "] [Schedule Debug] [Turn " << turn_cnt << "] Target request ID is "
                      << target << std::endl;
        }

        // 更新其他请求的权重
        for (auto& [req_id, weight] : req_weight)
        {
            if (req_id == target)
                continue;
            uint32_t v1 = SumKeyset(req_id, sid);
            uint32_t v2 = SumKeyset(target, sid);
            weight -= (req_weight[target] * v1 / v2);
        }
        // 将target加入结果，移出集合
        ret.push_back(target);
        req_set.erase(target);
        req_weight.erase(target);
    }
    // 结果逆序
    std::reverse(ret.begin(), ret.end());

    // 打印最终调度顺序
    ss.str("");
    ss << "[";
    for (const auto& req_id : ret)
        ss << "(req_id=" << req_id << ")";
    ss << "]";
    std::cout << "[Controller][" << Simulator::Now().As(Time::S)
              << "] Schedule completed(size=" << ret.size() << "): " << ss.str() << std::endl;

    return ret;
}

// 调度主流程
void
Controller::DoSchedule(SdnKvsHeader& sdnKvsHeader,
                       struct ofl_msg_packet_in* msg,
                       Ptr<const RemoteSwitch> swtch,
                       uint32_t xid)
{
    // 统计当前控制器队列中所有key的数量
    uint32_t tot_key_num = 0;
    for (const auto& [server_id, ctx] : m_scheduleContexts)
        for (const auto& pkt : ctx.schedulePackets)
            tot_key_num += pkt.sdnKvsHeader.GetKsSiz();
    // 打印收到的包信息
    std::stringstream ss;
    sdnKvsHeader.Print(ss);
    std::cout << "[Controller][" << Simulator::Now().As(Time::S) << "] Schedule client packet"
              << ss.str() << "(ctrl_queue_siz=" << tot_key_num << ")" << std::endl;
    // 获取目标服务器IP
    ss.str("");
    Ipv4Address serverIp = ExtractIpv4Address(OXM_OF_IPV4_DST, (struct ofl_match*)msg->match);
    ss << serverIp;

    // 构造调度包
    SchedulePacket schedulePacket;
    schedulePacket.id = ++m_idCnt;
    schedulePacket.sdnKvsHeader = sdnKvsHeader;
    schedulePacket.msg = msg;
    schedulePacket.swtch = swtch;
    schedulePacket.xid = xid;
    schedulePacket.arriveTime = Simulator::Now(); // 记录到达时间
    schedulePacket.dstServerId = m_ipToServer[ss.str()];

    // 将调度包加入目标服务器的调度队列
    ScheduleContext& context = m_scheduleContexts[schedulePacket.dstServerId];
    context.schedulePackets.push_back(schedulePacket);

    // 统计每个请求的最大等待时间和key总数
    std::map<uint32_t, int64_t> req_wait_time; // <请求id, 最大等待时间>
    std::map<uint32_t, uint32_t> req_key_num;  // <请求id, key总数>
    for (auto& [server_id, ctx] : m_scheduleContexts)
    {
        if (!ctx.is_heavy) // 只处理超载服务器
            continue;
        int64_t dt = Simulator::Now().GetMilliSeconds() - ctx.lastScheduleTime.GetMilliSeconds();
        if (dt >= 1.00) // 1ms is the minimum delta time.
        {
            uint32_t queue_size = ctx.queue_size;
            // Update each overloaded server's queue size
            ctx.queue_size = std::max(0U, uint32_t(queue_size - ctx.server_rate * dt));
            ctx.lastScheduleTime = Simulator::Now();
        }
        // Prepare for BP-LPT algorithm. Initialize all requests' wait times.
        for (auto& packet : ctx.schedulePackets)
        {
            uint32_t req_id = packet.sdnKvsHeader.GetReqId();
            int64_t wait_time =
                Simulator::Now().GetMilliSeconds() - packet.arriveTime.GetMilliSeconds();
            if (!req_wait_time.count(req_id))
                req_wait_time[req_id] = 0;
            req_wait_time[req_id] = std::max(req_wait_time[req_id], wait_time);
            if (!req_key_num.count(req_id))
                req_key_num[req_id] = 0;
            req_key_num[req_id] += packet.sdnKvsHeader.GetKsSiz();
        }
    }

    // Waiting for the scheduling requests list. Sorted in scheduling.
    std::vector<ScheduleRequest> scheduleRequests;
    for (const auto& [req_id, wait_time] : req_wait_time)
    {
        ScheduleRequest scheduleRequest;
        scheduleRequest.req_id = req_id;
        // scheduleRequest.weight = 1.0 + 2.0 * wait_time; // 权重计算方式
        scheduleRequest.weight = std::min(1.0 + 5.0 * wait_time / 1000, m_eta);
        scheduleRequest.key_num = req_key_num[req_id];
        scheduleRequests.push_back(scheduleRequest);
    }
    // 调用BP-LPT算法获得调度顺序
    std::vector<uint32_t> sortedReuests = BP_LPT(scheduleRequests);

    // 构建请求id到包指针的快照映射，以及每台服务器的key统计
    std::map<uint32_t, std::vector<SchedulePacket*>> pktSnapshot;
    std::map<uint32_t, uint32_t> ksStat; // Map<server id, the number of keys>
    // Prebuild packet snapshot data structure
    for (auto& [server_id, ctx] : m_scheduleContexts)
    {
        uint32_t ks_sum = 0; // 统计每台服务器的key数
        for (auto& pkt : ctx.schedulePackets)
        {
            uint32_t req_id = pkt.sdnKvsHeader.GetReqId();
            if (!pktSnapshot.count(req_id))
                pktSnapshot[req_id] = std::vector<SchedulePacket*>();
            pktSnapshot[req_id].push_back(&pkt);
            ks_sum += pkt.sdnKvsHeader.GetKsSiz();
        }
        ksStat[server_id] = ks_sum;
    }

    // 打印服务器key统计信息
    if (m_debug)
    {
        ss.str("");
        ss << "[";
        for (const auto& [server_id, ks_sum] : ksStat)
            ss << "(server_id=" << server_id << ", ks_sum=" << ks_sum << ")";
        ss << "]";
        std::cout << "[Controller][" << Simulator::Now().As(Time::S)
                  << "] Server keys: " << ss.str() << std::endl;
    }

    std::map<uint32_t, uint32_t> dt_base; // Delta send time base count. Map<server id, base count>
    // Total ks_siz for specific server. Used for set sending time. Map<server id, total ks_siz>
    std::map<uint32_t, uint32_t> cur_ks_siz;
    // Last send time for specific server. Used for set sending time. Map<server id, last send time>
    std::map<uint32_t, Time> lst_dt;

    // dt = queue_size / server_rate + base_value * base_count. Here base_value=0.005ms
    for (uint32_t i = 0; i < m_serverNodes.size(); i++)
        dt_base[i] = 0, cur_ks_siz[i] = 0;
    // 按调度顺序遍历每个请求
    // 遍历排序后的请求ID列表，按照BP-LPT算法得到的调度顺序依次处理每个请求
    for (const auto& req_id : sortedReuests)
    {
        // 对于每个请求，遍历其对应的所有待调度数据包指针
        for (const auto& pkt : pktSnapshot[req_id])
        {
            // 清空字符串流，用于打印包头信息
            ss.str("");
            // 打印当前包的SDN-KVS头部信息到字符串流
            pkt->sdnKvsHeader.Print(ss);

            Time dt; // 记录当前包的调度发送时间
            // 获取该包目标服务器的调度上下文
            ScheduleContext& ctx = m_scheduleContexts[pkt->dstServerId];

            // // 判断当前服务器的key数是否超过该服务器的阈值（server_tqs）
            // if (cur_ks_siz[pkt->dstServerId] >
            //     ctx.server_tqs) // modify by wangkai  ctx.server_tqs  m_minQ
            // {
            //     // 如果超过阈值，重置该服务器的基数（dt_base），用于后续延迟计算
            //     dt_base[pkt->dstServerId] = 0;
            //     // 计算超出部分的延迟ddt，包含超出key数/服务速率和基数微小扰动
            //     Time ddt = Seconds(
            //         (cur_ks_siz[pkt->dstServerId] - ctx.server_tqs) / (1000.0 * ctx.server_rate)
            //         + 0.000005 * dt_base[pkt->dstServerId]); // modify by wangkai  ctx.server_tqs
            //     // 该包的调度时间为上次调度时间加上新计算的延迟
            //     dt = lst_dt[pkt->dstServerId] + ddt;
            //     // 更新当前服务器的key数为阈值加上本包的key数
            //     cur_ks_siz[pkt->dstServerId] =
            //         ctx.server_tqs +
            //         pkt->sdnKvsHeader.GetKsSiz(); // modify by wangkai ctx.server_tqs
            // }
            // else
            // {
            //     // 如果未超过阈值，直接根据队列长度和服务速率计算延迟
            //     dt = Seconds(std::max(int32_t(ctx.queue_size) - int32_t(ctx.server_tqs), 0) /
            //                      (1000.0 * ctx.server_rate) +
            //                  0.000005 * dt_base[pkt->dstServerId]);
            //     // modify by wangkai
            //     //  dt = Seconds(std::max(int32_t(ctx.queue_size) - int32_t(ctx.server_tqs), 0) /
            //     //                   (1000.0 * ctx.server_rate) +
            //     //               0.000005 * dt_base[pkt->dstServerId]);
            //     //  累加当前服务器的key数
            //     cur_ks_siz[pkt->dstServerId] += pkt->sdnKvsHeader.GetKsSiz();
            // }

            if (cur_ks_siz[pkt->dstServerId] > 0)
            {
                Time ddt = Seconds(cur_ks_siz[pkt->dstServerId] /
                                   (1000.0 * ctx.server_rate)); // modify by wangkai  ctx.server_tqs
                dt = lst_dt[pkt->dstServerId] + ddt;
                cur_ks_siz[pkt->dstServerId] = pkt->sdnKvsHeader.GetKsSiz();
            }
            else
            {
                dt = Seconds(std::max(int32_t(ctx.queue_size) - int32_t(m_minQ), 0) /
                             (1000.0 * ctx.server_rate));
                cur_ks_siz[pkt->dstServerId] = pkt->sdnKvsHeader.GetKsSiz();
            }

            // 基数自增，用于下一个包的扰动项
            dt_base[pkt->dstServerId] += 1;
            // 记录该服务器上次调度的时间
            lst_dt[pkt->dstServerId] = dt;

            // 如果该包之前已经安排了发送事件，则先取消之前的事件，避免重复发送
            if (pkt->eventId.IsRunning())
            {
                if (m_debug)
                {
                    std::cout << "[Controller][" << Simulator::Now().As(Time::S)
                              << "] Cancel the send event for Packet(pkt_id=" << pkt->id << ")"
                              << ss.str() << std::endl;
                }
                pkt->eventId.Cancel();
            }
            // 安排新的发送事件，将包在dt时间后发送到目标服务器
            pkt->eventId =
                Simulator::Schedule(dt, &Controller::RemovePacketAndForward, this, *pkt, req_id);
            // 如果开启调试，打印调度信息
            if (m_debug)
            {
                std::cout << "[Controller][" << Simulator::Now().As(Time::S)
                          << "] Packet(pkt_id=" << pkt->id << ")" << ss.str()
                          << " will be forwarded to the server " << pkt->dstServerId << " in "
                          << dt.As(Time::S) << std::endl;
            }
        }
    }
    // Do not free the message.
    // Message will be free in RemovePacketAndForward function
}

void
Controller::DoFlowModify(SdnKvsHeader& sdnKvsHeader,
                         struct ofl_msg_packet_in* msg,
                         Ptr<const RemoteSwitch> swtch,
                         uint32_t xid)
{
    std::stringstream ss;
    // Get the switch datapath ID
    uint64_t swDpId = swtch->GetDpId();
    // Get server IP address
    Ipv4Address serverIp = ExtractIpv4Address(OXM_OF_IPV4_SRC, (struct ofl_match*)msg->match);
    ss << serverIp;
    std::string serverIp_str = ss.str();
    uint32_t server_id = m_ipToServer[serverIp_str];
    // Check if this server is connected to the switch
    int32_t in_port = GetSwitchPortByServerId(swDpId, server_id);

    // Modify the flow table when the server is heavy and is connected to this switch
    if (sdnKvsHeader.GetHeavy() == 1 && in_port != -1)
    {
        // Record the overload time, queue size and server service rate when overloaded
        ScheduleContext& context = m_scheduleContexts[server_id];
        context.queue_size = sdnKvsHeader.GetQueueSiz();
        context.server_rate = sdnKvsHeader.GetServerRate();
        context.server_tqs = sdnKvsHeader.GetServerTqs();
        context.lastScheduleTime = Simulator::Now();
        context.is_heavy = true;

        // The server is overloaded. Modify the flow table to make each packet
        // sent from the client to be redirected to the controller.
        std::cout << "[Controller][" << Simulator::Now().As(Time::S) << "] Server " << server_id
                  << "(ip=" << serverIp_str << ", queue_siz=" << context.queue_size
                  << ", heavy_time=" << context.lastScheduleTime.As(Time::S)
                  << ", server_rate=" << context.server_rate
                  << ") is overloaded. Modify flow table for switch " << swDpId << std::endl;
        ss.str(""); // Clear string stream
        ss << "flow-mod cmd=mod,table=0,prio=100 "
           << "eth_type=0x0800,ip_dst=" << serverIp_str << " apply:output=ctrl:128";
        DpctlExecute(swDpId, ss.str());

        // Cancel the undo event if there is already an event set before.
        ScheduleContext& ctx = m_scheduleContexts[server_id];
        if (ctx.undoFlowEvents[swDpId].IsRunning())
        {
            if (m_debug)
            {
                std::cout << "[Controller][" << Simulator::Now().As(Time::S)
                          << "] Cancel undo flow table modification event for switch " << swDpId
                          << " (server_id=" << server_id << ")" << std::endl;
            }
            ctx.undoFlowEvents[swDpId].Cancel();
        }

        // Set timer to undo modification.
        // modify by wangkai 流表恢复写到出队的时候，如果当前队列小于minQ，就恢复
        Time dt =
            Seconds(1.0 * (std::max(0, int32_t(sdnKvsHeader.GetQueueSiz()) - int32_t(m_minQ))) /
                    (1000.0 * sdnKvsHeader.GetServerRate())); // Equation (1)    Mq=5
        std::cout << "[Controller][" << Simulator::Now().As(Time::S) << "] " << dt.As(Time::S)
                  << " later will undo the modification of the flow table" << std::endl;

        // std::cout << "[Controller] Set undoFlowEventsFlag for (swDpId=" << swDpId
        //           << ", serverIp=" << serverIp_str << ")" << std::endl;
        undoFlowEventsFlag[std::make_pair(uint32_t(swDpId), serverIp_str)] = 0;
        // 打印所有的undoFlowEventsFlag
        // for (const auto& kv : undoFlowEventsFlag)
        // {
        //     std::cout << "  (swDpId=" << kv.first.first << ", serverIp=" << kv.first.second
        //               << ") = " << kv.second << std::endl;
        // }

        ctx.undoFlowEvents[swDpId] =
            Simulator::Schedule(dt, &Controller::UndoFlowModifyFlag, this, swDpId, serverIp_str);
    }
    // Forward packet to client
    if (sdnKvsHeader.GetId() != 0)
        ForwardPacket(msg, swtch, xid);
    // Free message after forwarding packet
    ofl_msg_free((struct ofl_msg_header*)msg, nullptr);
}

void
Controller::UndoFlowModifyFlag(uint32_t switch_id, std::string server_ip)
{
    undoFlowEventsFlag[std::make_pair(switch_id, server_ip)] = 1;
}

void
Controller::UndoFlowModify(uint32_t switch_id, std::string server_ip)
{
    uint32_t server_id = m_ipToServer[server_ip];
    ScheduleContext& ctx = m_scheduleContexts[server_id];
    ctx.is_heavy = false;
    int32_t port = GetSwitchPortByServerId(switch_id, server_id);
    if (port == -1)
        return; // Should be unreachable

    std::cout << "[Controller][" << Simulator::Now().As(Time::S)
              << "] Undo the modification of the flow table(switch_id=" << switch_id
              << ", server_ip=" << server_ip << ")" << std::endl;

    std::stringstream ss;
    ss << "flow-mod cmd=mod,table=0,prio=100 "
       << "eth_type=0x0800,ip_dst=" << server_ip << " apply:nw_dec,output=" << port;
    DpctlExecute(switch_id, ss.str());
}

void
Controller::RemovePacketAndForward(SchedulePacket pkt, uint32_t req_id)
{
    // Remove packet from schdule list
    ScheduleContext& ctx = m_scheduleContexts[pkt.dstServerId];
    auto cond = [&pkt](SchedulePacket& packet) { return packet.id == pkt.id; };
    auto it = std::find_if(ctx.schedulePackets.begin(), ctx.schedulePackets.end(), cond);
    if (it != ctx.schedulePackets.end())
        ctx.schedulePackets.erase(it);

    uint64_t tot_key_num_server = 0;
    for (const auto& pkt : ctx.schedulePackets)
        tot_key_num_server += pkt.sdnKvsHeader.GetKsSiz();

    // Log the controller queue size
    uint32_t tot_key_num = 0;
    for (const auto& [server_id, ctx] : m_scheduleContexts)
        for (const auto& pkt : ctx.schedulePackets)
            tot_key_num += pkt.sdnKvsHeader.GetKsSiz();

    // Print log
    std::stringstream ss;
    pkt.sdnKvsHeader.Print(ss);
    std::cout << "[Controller][" << Simulator::Now().As(Time::S)
              << "] Forward packet(pkt_id=" << pkt.id << ")" << ss.str() << " to the server "
              << pkt.dstServerId << " (ctrl_queue_siz=" << tot_key_num << ")" << std::endl;

    if (tot_key_num_server < m_minQ)
    {
        Ipv4Address serverIp =
            ExtractIpv4Address(OXM_OF_IPV4_DST, (struct ofl_match*)pkt.msg->match);
        std::stringstream ip_ss;
        ip_ss << serverIp;
        std::string serverIp_str = ip_ss.str();

        uint64_t swDpId = pkt.swtch->GetDpId();

        // for (const auto& kv : undoFlowEventsFlag)
        // {
        //     std::cout << "  (swDpId=" << kv.first.first << ", serverIp=" << kv.first.second
        //               << ") = " << kv.second << std::endl;
        // }
        // Only undo if already done
        std::cout << "[Controller][" << Simulator::Now().As(Time::S) << "] "
                  << " shadow queue drains. undo the modification of the flow table"
                  << " (SumKeyset=" << tot_key_num_server << ", req_id=" << req_id
                  << ", dstServerId=" << pkt.dstServerId << ", swDpId=" << swDpId
                  << ", serverIp_str=" << serverIp_str << ")" << std::endl;
        //<< " undoFlag=" << undoFlowEventsFlag[std::make_pair(uint32_t(swDpId), serverIp_str)]
        auto key = std::make_pair(uint32_t(swDpId), serverIp_str);
        auto it = undoFlowEventsFlag.find(key);
        if (it != undoFlowEventsFlag.end() && it->second == int32_t(1))
        {
            UndoFlowModify(swDpId, serverIp_str);
            undoFlowEventsFlag[std::make_pair(uint32_t(swDpId), serverIp_str)] = 0;
        }
    }

    // Forward packet to the server
    ForwardPacket(pkt.msg, pkt.swtch, pkt.xid);
    // Free message after forwarding packet
    ofl_msg_free((struct ofl_msg_header*)pkt.msg, nullptr);
}

int
Controller::ForwardPacket(struct ofl_msg_packet_in* msg,
                          Ptr<const RemoteSwitch> swtch,
                          uint32_t xid)
{
    // Get the switch datapath ID
    uint64_t swDpId = swtch->GetDpId();
    // Get input port
    uint32_t inPort;
    struct ofl_match_tlv* tlv = oxm_match_lookup(OXM_OF_IN_PORT, (struct ofl_match*)msg->match);
    memcpy(&inPort, tlv->value, OXM_LENGTH(OXM_OF_IN_PORT));
    // Get destination IP address
    Ipv4Address dstIp = ExtractIpv4Address(OXM_OF_IPV4_DST, (struct ofl_match*)msg->match);
    // Get output port
    uint32_t outPort = GetSwitchPortByIPv4(swDpId, dstIp);

    struct ofl_action_output* action =
        (struct ofl_action_output*)xmalloc(sizeof(struct ofl_action_output));
    action->header.type = OFPAT_OUTPUT;
    action->port = outPort;
    action->max_len = 0;

    // Send the reply within an OpenFlow PacketOut message
    struct ofl_msg_packet_out reply;
    reply.header.type = OFPT_PACKET_OUT;
    reply.buffer_id = NO_BUFFER;
    reply.in_port = inPort;
    reply.actions_num = 1;
    reply.actions = (struct ofl_action_header**)&action;
    // No packet buffer. Send data back to switch
    reply.data_length = msg->data_length;
    reply.data = msg->data;

    int ret = SendToSwitch(swtch, (struct ofl_msg_header*)&reply, xid);

    std::cout << "[Controller][" << Simulator::Now().As(Time::S)
              << "] Sent Packet-Out packet: (buffer_id=" << reply.buffer_id
              << ", in_port=" << reply.in_port << ", out_port=" << action->port << ") to switch "
              << swtch->GetDpId() << std::endl;
    free(action);
    return ret;
}

std::string
Controller::GetIpv4AddressFromNode(Ptr<Node> node)
{
    std::stringstream ss;
    ss << node->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();
    return ss.str();
}

Ipv4Address
Controller::ExtractIpv4Address(uint32_t oxm_of, struct ofl_match* match)
{
    switch (oxm_of)
    {
    case static_cast<uint32_t>(OXM_OF_ARP_SPA):
    case static_cast<uint32_t>(OXM_OF_ARP_TPA):
    case static_cast<uint32_t>(OXM_OF_IPV4_DST):
    case static_cast<uint32_t>(OXM_OF_IPV4_SRC): {
        uint32_t ip;
        int size = OXM_LENGTH(oxm_of);
        struct ofl_match_tlv* tlv = oxm_match_lookup(oxm_of, match);
        memcpy(&ip, tlv->value, size);
        return Ipv4Address(ntohl(ip));
    }
    default:
        NS_ABORT_MSG("Invalid IP field.");
    }
}

int32_t
Controller::GetSwitchPortByClientId(uint64_t switch_id, uint32_t client_id)
{
    if (switch_id != client_id % 2 + 1)
        return -1;
    // even client_id: (port - 1) * 2 = client_id  ==>  port = client_id / 2 + 1
    // odd client_id: 2 * port - 1 = client_id  ==>  port = (client_id + 1) / 2
    if (client_id % 2 == 0)
        return client_id / 2 + 1;
    else
        return (client_id + 1) / 2;
}

int32_t
Controller::GetSwitchPortByServerId(uint64_t switch_id, uint32_t server_id)
{
    uint32_t client_num = m_clientNodes.size();
    uint32_t global_id = client_num + server_id;
    if (switch_id != global_id % 2 + 1)
        return -1;
    // even global_id: (port - 1) * 2 = global_id  ==>  port = global_id / 2 + 1
    // odd global_id: 2 * port - 1 = global_id  ==>  port = (global_id + 1) / 2
    if (global_id % 2 == 0)
        return global_id / 2 + 1;
    else
        return (global_id + 1) / 2;
}

int32_t
Controller::GetSwitchPortByIPv4(uint64_t switch_id, Ipv4Address& ipv4)
{
    std::stringstream ss;
    ss << ipv4;
    std::string ip = ss.str();
    if (m_ipToServer.count(ip))
    {
        uint32_t server_id = m_ipToServer[ip];
        int32_t ret = GetSwitchPortByServerId(switch_id, server_id);
        return (ret == -1) ? m_switchPortCnt[switch_id - 1] : ret;
    }
    uint32_t client_id = m_ipToClient[ip];
    int32_t ret = GetSwitchPortByClientId(switch_id, client_id);
    return (ret == -1) ? m_switchPortCnt[switch_id - 1] : ret;
}

uint32_t
Controller::SumKeyset(std::set<uint32_t>& req_set, uint32_t server_id)
{
    uint32_t ret = 0;
    const ScheduleContext& ctx = m_scheduleContexts[server_id];
    for (const auto& pkt : ctx.schedulePackets)
    {
        uint32_t req_id = pkt.sdnKvsHeader.GetReqId();
        if (!req_set.count(req_id))
            continue;
        ret += pkt.sdnKvsHeader.GetKsSiz();
    }
    return ret;
}

uint32_t
Controller::SumKeyset(uint32_t req_id, uint32_t server_id)
{
    uint32_t ret = 0;
    const ScheduleContext& ctx = m_scheduleContexts[server_id];
    for (const auto& pkt : ctx.schedulePackets)
    {
        if (pkt.sdnKvsHeader.GetReqId() != req_id)
            continue;
        ret += pkt.sdnKvsHeader.GetKsSiz();
    }
    return ret;
}

uint32_t
Controller::FindBottleneckServer(std::set<uint32_t>& req_set)
{
    // Assert that there is at least one server overloaded and can always get the answer
    // when this function is called.
    double mx = 0;
    uint32_t ret = 0;
    uint32_t server_num = m_serverNodes.size();
    for (uint32_t i = 0; i < server_num; i++)
    {
        const ScheduleContext& ctx = m_scheduleContexts[i];
        uint32_t kv = SumKeyset(req_set, i);
        // This server is not overloaded or no packets
        if (!ctx.is_heavy || kv == 0)
            continue;
        // The weight computed here is always greater than or equal to 0
        double weight = (kv + ctx.queue_size) * 1.0 / ctx.server_rate;
        if (weight < mx)
            continue;
        mx = weight, ret = i;
    }
    return ret;
}

std::ostream&
operator<<(std::ostream& os, Controller::ScheduleContext ctx)
{
    std::stringstream ss;
    ss << "[";
    for (const auto& pkt : ctx.schedulePackets)
        ss << pkt;
    ss << "]";
    os << "queue_size=" << ctx.queue_size << ", server_rate=" << ctx.server_rate
       << ", is_heavy=" << ctx.is_heavy
       << ", lst_schedule_time=" << ctx.lastScheduleTime.As(Time::S) << ", packets=" << ss.str();
    return os;
}

std::ostream&
operator<<(std::ostream& os, Controller::SchedulePacket pkt)
{
    os << "(id=" << pkt.sdnKvsHeader.GetId() << ", req_id=" << pkt.sdnKvsHeader.GetReqId()
       << ", ks_siz=" << pkt.sdnKvsHeader.GetKsSiz() << ")";
    return os;
}
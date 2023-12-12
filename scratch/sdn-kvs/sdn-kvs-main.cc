#include "client-helper.h"
#include "controller.h"
#include "model.h"
#include "server-helper.h"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-apps-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/ofswitch13-module.h"

#include <numeric>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("SDN-KVS-MAIN");

/**
 * Parameter configure
 */
SchedulingType algorithm = SchedulingStrategy::FIFO;
bool debug = false;                                   // Enable DEBUG log
bool ifZipfian = true;                                // Whether there is a hotspot server
DistributionType req_size_distribution_type = PARETO; // The distribution type for generating key
KeysetSizeType keyset_size_type = HEAVY;              // The keyset size type
KeysetSizeType sendtime_type = HEAVY;                 // The send time type
double uti = 0.7;                                     // Utilization
uint32_t simTime = 1000;                              // Simulator running time(unit: second)
uint32_t server_rate = 1;     // Server handle rate. (Number of keys processed per millisecond)
uint16_t server_port = 25565; // Server listen port
uint32_t server_tqs = 64;     // Server TQS
uint32_t client_num = 15;     // The number of client node
uint32_t server_num = 15;     // The number of server node
uint32_t key_num = 50000;     // The upper bound of the total number of keys(n keys = 1 keyset)
static const int request_size = 30;
/**
 * Global variable
 */
uint32_t tot_key_num = 0;     // The total number of keys(it is also the sum of keyset size)
uint32_t tot_request_num = 0; // The total number of requests
uint32_t tot_ks_num = 0;      // The total number of keyset
std::map<uint32_t, std::vector<RequestTableEntry>> req_table; // Key: client id

/**
 * Generate the number of keys in a request. It is guaranteed to be less than or equal to key_num.
 */
uint32_t
generate_req_size()
{
    uint32_t ret = 0;
    switch (req_size_distribution_type)
    {
    case POSSION: {
        Ptr<UniformRandomVariable> x = CreateObject<UniformRandomVariable>();
        x->SetAttribute("Min", DoubleValue(request_size - 5));
        x->SetAttribute("Max", DoubleValue(request_size + 5));
        ret = x->GetInteger();
        break;
    }
    case PARETO: {
        double rand_value = rand() / double(RAND_MAX);
        double alpha = 2.0;
        uint32_t x_min = uint32_t(request_size * (alpha - 1) / alpha);
        uint32_t req_size = uint32_t(x_min / pow((1 - rand_value), (1 / alpha)));
        ret = req_size > 100 ? 100 : req_size;
        break;
    }
    }
    return ret > key_num ? key_num : ret;
}

/**
 * Generate the number of keyset in a request.
 */
uint32_t
generate_ks_num()
{
    if (server_num <= 1)
        return 1;
    uint32_t value = 0;
    Ptr<ParetoRandomVariable> x = CreateObject<ParetoRandomVariable>();
    x->SetAttribute("Shape", DoubleValue(1.2));
    x->SetAttribute("Bound", DoubleValue(9.0));
    uint32_t y = x->GetInteger();
    if (y >= server_num)
        value = server_num;
    else if (y == 1)
        value = 2;
    else
        value = y;
    return value;
}

uint32_t
generate_ks_siz(int ks_num)
{
    uint32_t ret = 0;
    switch (keyset_size_type)
    {
    case HEAVY: {
        ret = -1;
        while (true)
        {
            double rand_value = rand() / double(RAND_MAX);
            double alpha = 2.0;
            double mean = double(request_size / double(ks_num));
            double x_min = (alpha - 1) * mean / alpha;
            ret = uint32_t(x_min / pow((1 - rand_value), (1 / alpha)));
            ret = ret > 100 ? 100 : ret;
            if (ret != 0)
                break;
        }
        break;
    }
    case LIGHT: {
        Ptr<UniformRandomVariable> x = CreateObject<UniformRandomVariable>();
        x->SetAttribute("Min", DoubleValue(request_size / double(ks_num) - 3));
        x->SetAttribute("Max", DoubleValue(request_size / double(ks_num) + 3));
        uint32_t value = x->GetInteger();
        ret = value <= 0 ? 1 : value;
        break;
    }
    }
    return ret;
}

/**
 * Generate destination server id(start from 0) for keyset
 */
uint32_t
generate_dst_server(std::unordered_map<uint32_t, bool>& is_add)
{
    uint32_t ret;
    while (true)
    {
        uint32_t x = 0;
        if (ifZipfian == true)
        {
            Ptr<ZipfRandomVariable> zipf = CreateObject<ZipfRandomVariable>();
            zipf->SetAttribute("N", IntegerValue(10));
            zipf->SetAttribute("Alpha", DoubleValue(2.0));
            int value = zipf->GetInteger();
            int value1 = server_num / 5;
            // 生成 value1 * (value - 1) 到 value1 * value 之间的随机数
            int value2 = (rand() % (value1 + 1)) + value1 * (value - 1);
            x = value2 % server_num + client_num + 1;
        }
        else
        {
            x = (rand() % server_num) + client_num + 1;
        }
        if (is_add[x] == false && x >= (client_num + 1))
        {
            ret = x;
            is_add[x] = true;
            break;
        }
    }
    return ret - client_num - 1; // Start from 0
}

// 数据包到达时间是possion过程，相邻数据包的发送间隔服从指数分布
double
poisson(double lambda)
{
    double pV = 0.0;
    while (true)
    {
        pV = (double)rand() / (double)RAND_MAX;
        if (pV != 1)
        {
            break;
        }
    }
    double interval = (-1.0 / lambda) * log(1 - pV);
    return interval;
}

// Generate heavy-tailed distributions of request intervals
Time
GenerateArriveIntervalPareto(double mean)
{
    Ptr<ParetoRandomVariable> pArrival = CreateObject<ParetoRandomVariable>();
    pArrival->SetAttribute("Scale", DoubleValue(mean));
    pArrival->SetAttribute("Shape", DoubleValue(2.0));
    // pArrival->SetAttribute("Bound", DoubleValue(0.5));
    double value = pArrival->GetValue();
    Time tNext(Seconds(value));
    return tNext;
}

/**
 * Prebuild request table
 * \param serverNodes Node container of server nodes. It is used to get IPv4 address from node.
 */
void
init_request_table(NodeContainer& serverNodes)
{
    std::vector<RequestTableEntry> vec; // Temporary vector to store entry
    uint32_t req_id = 1;
    while (key_num > 0)
    {
        std::vector<uint32_t> keyset; // All keysets generated for this request

        // The client that this request was sent from
        uint32_t client_id = rand() % client_num;
        if (req_table.count(client_id) == 0)
            req_table[client_id] = std::vector<RequestTableEntry>();
        // The upper bound of request size
        uint32_t req_siz = generate_req_size();
        uint32_t ks_num = generate_ks_num(); // The upper bound of the keyset number of this request

        // Generate keyset for request. The generated keyset's size and number will not be greater
        // than the upper bound.
        uint32_t iter_ks_siz = 0;       // Each keyset size generated
        uint32_t bottleneck_keyset = 0; // bottleneck keyset size
        for (uint32_t i = req_siz, j = 0; i > 0 && j < ks_num; i -= iter_ks_siz, j++)
        {
            iter_ks_siz = generate_ks_siz(ks_num);
            if (iter_ks_siz > i)
                iter_ks_siz = i;

            if (iter_ks_siz > bottleneck_keyset)
                bottleneck_keyset = iter_ks_siz;

            keyset.push_back(iter_ks_siz);
        }
        // Count the total size and total number of keysets
        tot_key_num += std::accumulate(keyset.begin(), keyset.end(), 0);
        tot_ks_num += keyset.size();
        std::unordered_map<uint32_t, bool> is_add;
        for (uint32_t ks_siz : keyset)
        {
            // The server that this keyset data packet was sent to
            uint32_t server_id = generate_dst_server(is_add);
            // Get IPv4 address from server id
            Ptr<Node> serverNode = serverNodes.Get(server_id);
            std::stringstream ss;
            ss << serverNode->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();

            // Build request table entry
            RequestTableEntry entry;
            entry.st = 0.0; // Initialize it later
            entry.src_client = client_id;
            entry.dst_server = server_id;
            entry.dst_ip = ss.str();
            entry.dst_port = server_port;
            entry.req_id = req_id;
            entry.ks_siz = ks_siz;
            entry.bottleneck_ks_size = bottleneck_keyset;

            // Record this entry to the temporary vector
            vec.push_back(entry);
        }

        key_num -= req_siz;
        ++tot_request_num; // Count the total number of requests
        if (key_num <= 0)
            break;
        req_id++;
    }

    // Initialize send time
    // double req_mean = 1.0 * tot_key_num / tot_ks_num;
    double req_mean = request_size;
    double send_time = 2;
    double t_mean = (req_mean * 1.0 / (server_rate * 1000.0)) / (uti * double(server_num));
    std::map<uint32_t, double> send_time_mp; // Map<request id, send time>
    for (uint32_t req_id = 1; req_id <= tot_request_num; req_id++)
    {
        double double_tNext;
        switch (sendtime_type)
        {
        case LIGHT: {
            double r = 0;
            while (true)
            {
                r = poisson(1.0 / t_mean);
                if (r != 0)
                    break;
            }
            double_tNext = Seconds(r).GetSeconds();
        }
        break;
        case HEAVY:
        default: {
            double_tNext = GenerateArriveIntervalPareto(t_mean).GetSeconds();
        }
        break;
        }
        send_time += double_tNext;
        send_time_mp[req_id] = send_time;
    }

    // Fill send time for each request table entry,
    // and insert each entry to the request table
    for (auto& entry : vec)
    {
        entry.st = send_time_mp[entry.req_id];
        req_table[entry.src_client].push_back(entry);
    }

    // Sort vector by send time
    auto cmp = [](RequestTableEntry& entry1, RequestTableEntry& entry2) -> bool {
        return entry1.st < entry2.st;
    };
    for (auto& [client_id, vec] : req_table)
        std::sort(vec.begin(), vec.end(), cmp);

    // Print the request table
    NS_LOG_UNCOND("tot_key_num = " << tot_key_num << ", tot_request_num = " << tot_request_num
                                   << ", tot_ks_num = " << tot_ks_num);
    for (const auto& [client_id, entry_vec] : req_table)
    {
        for (const auto& entry : entry_vec)
        {
            std::stringstream ss;
            ss << "[" << client_id << "] (st=" << entry.st << ", src_client=" << entry.src_client
               << ", dst_server=" << entry.dst_server << ", dst_ip=" << entry.dst_ip
               << ", req_id=" << entry.req_id << ", ks_siz=" << entry.ks_siz
               << ", bottleneck_ks_size=" << entry.bottleneck_ks_size << ")";
            NS_LOG_UNCOND(ss.str());
        }
    }
}

void
init_parameter(int argc, char* argv[])
{
    CommandLine cmd;
    uint32_t algorithm_index = 0;         // Default: FIFO
    uint32_t distribution_type_index = 0; // Default: POSSION
    uint32_t keyset_size_type_index = 0;  // Default: HEAVY
    uint32_t sendtime_type_index = 0;     // Default: HEAVY
    cmd.AddValue("algorithm",
                 "Algorithm(0: FIFO, 1: SRPT, 2: SBF, 3: DAS, 4: SDN_KVS)",
                 algorithm_index);
    cmd.AddValue("debug", "Enable DEBUG log", debug);
    cmd.AddValue("ifZipfian", "Whether there is a hotspot server", ifZipfian);
    cmd.AddValue("distribution_type",
                 "The distribution type for generating key(0: POSSION, 1: PARETO)",
                 distribution_type_index);
    cmd.AddValue("keyset_size_type",
                 "The keyset size type(0: HEAVY, 1: LIGHT)",
                 keyset_size_type_index);
    cmd.AddValue("sendtime_type", "The send time type(0: HEAVY, 1: LIGHT)", sendtime_type_index);
    cmd.AddValue("uti", "Utilization", uti);
    cmd.AddValue("simTime", "Simulator running time(unit: second)", simTime);
    cmd.AddValue("server_rate",
                 "Server handle rate. (Number of keys processed per millisecond)",
                 server_rate);
    cmd.AddValue("server_tqs", "Server TQS", server_tqs);
    cmd.AddValue("client_num", "The number of client node", client_num);
    cmd.AddValue("server_num", "The number of server node", server_num);
    cmd.AddValue("key_num",
                 "The upper bound of the total number of keys(n keys = 1 keyset)",
                 key_num);
    cmd.Parse(argc, argv);

    switch (algorithm_index)
    {
    case 1:
        algorithm = SchedulingStrategy::SRPT;
        break;
    case 2:
        algorithm = SchedulingStrategy::SBF;
        break;
    case 3:
        algorithm = SchedulingStrategy::DAS;
        break;
    case 4:
        algorithm = SchedulingStrategy::SDN_KVS;
        break;
    case 0:
    default:
        algorithm = SchedulingStrategy::FIFO;
    }

    switch (distribution_type_index)
    {
    case 1:
        req_size_distribution_type = PARETO;
        break;
    case 0:
    default:
        req_size_distribution_type = POSSION;
    }

    switch (keyset_size_type_index)
    {
    case 1:
        keyset_size_type = LIGHT;
        break;
    case 0:
    default:
        keyset_size_type = HEAVY;
    }

    switch (sendtime_type_index)
    {
    case 1:
        sendtime_type = LIGHT;
        break;
    case 0:
    default:
        sendtime_type = HEAVY;
    }

    // Print the parameter
    NS_LOG_UNCOND("===== Parameter configure =====");
    NS_LOG_UNCOND(" - algorithm = " << algorithm);
    NS_LOG_UNCOND(" - debug = " << debug);
    NS_LOG_UNCOND(" - ifZipfian = " << ifZipfian);
    NS_LOG_UNCOND(" - distribution_type = " << req_size_distribution_type);
    NS_LOG_UNCOND(" - keyset_size_type = " << keyset_size_type);
    NS_LOG_UNCOND(" - sendtime_type = " << sendtime_type);
    NS_LOG_UNCOND(" - uti = " << uti);
    NS_LOG_UNCOND(" - simTime = " << simTime);
    NS_LOG_UNCOND(" - server_rate = " << server_rate);
    NS_LOG_UNCOND(" - server_tqs = " << server_tqs);
    NS_LOG_UNCOND(" - client_num = " << client_num);
    NS_LOG_UNCOND(" - server_num = " << server_num);
    NS_LOG_UNCOND(" - key_num = " << key_num);
    NS_LOG_UNCOND(" - request_size = " << request_size);
}

int
main(int argc, char* argv[])
{
    init_parameter(argc, argv);

    // IMPORTANT: CSMA will drop some packets without this line
    Config::SetDefault("ns3::ArpCache::PendingQueueSize", UintegerValue(10000));
    Config::SetDefault("ns3::ArpCache::WaitReplyTimeout", TimeValue(Seconds(10)));
    Config::SetDefault("ns3::ArpCache::MaxRetries", UintegerValue(300));

    if (debug)
    {
        // DEBUG LOG
        OFSwitch13Helper::EnableDatapathLogs();
        LogComponentEnable("OFSwitch13Device", LOG_LEVEL_ALL);
        LogComponentEnable("OFSwitch13Port", LOG_LEVEL_ALL);
        LogComponentEnable("OFSwitch13Queue", LOG_LEVEL_ALL);
        LogComponentEnable("OFSwitch13SocketHandler", LOG_LEVEL_ALL);
        LogComponentEnable("OFSwitch13Controller", LOG_LEVEL_ALL);
        LogComponentEnable("OFSwitch13LearningController", LOG_LEVEL_ALL);
        LogComponentEnable("OFSwitch13Helper", LOG_LEVEL_ALL);
        LogComponentEnable("OFSwitch13InternalHelper", LOG_LEVEL_ALL);
    }
    LogComponentEnable("Controller", LOG_LEVEL_ALL);
    LogComponentEnable("ServerApplication", LOG_LEVEL_ALL);
    LogComponentEnable("ClientApplication", LOG_LEVEL_ALL);

    // Enable checksum computations (required by OFSwitch13 module)
    GlobalValue::Bind("ChecksumEnabled", BooleanValue(true));

    // Create nodes for servers, switches and clients
    NodeContainer serverNodes;
    NodeContainer clientNodes;
    NodeContainer switchNodes;
    serverNodes.Create(server_num);
    clientNodes.Create(client_num);
    switchNodes.Create(2);
    // Create the controller node
    Ptr<Node> controllerNode = CreateObject<Node>();
    // Initialize the SDN-KVS controller
    Ptr<Controller> controller = CreateObject<Controller>();
    // Whether SDN-KVS algorithm is enabled on the controller
    bool enable_sdn_kvs = algorithm.controller_algorithm == ControllerSchedulingType::SDN_KVS;
    controller->SetSdnKvsEnabled(enable_sdn_kvs);
    // Use the CsmaHelper to connect client or server nodes to the switch node
    CsmaHelper csmaHelper;
    csmaHelper.SetChannelAttribute("DataRate", DataRateValue(DataRate("100Gbps")));
    csmaHelper.SetChannelAttribute("Delay", TimeValue(MicroSeconds(20)));

    // Net devices of all nodes for assigning IPv4 address later
    NetDeviceContainer nodeDevices;
    // Net devices of switch node
    NetDeviceContainer switchNodeDevices[2];

    // Connect client to the switch
    for (uint32_t i = 0; i < client_num; i++)
    {
        Ptr<Node> clientNode = clientNodes.Get(i);
        Ptr<Node> switchNode = switchNodes.Get(i % 2);
        NodeContainer pair(clientNode, switchNode);
        NetDeviceContainer link = csmaHelper.Install(pair);
        nodeDevices.Add(link.Get(0)); // Add client node's net device to the vector
        switchNodeDevices[i % 2].Add(link.Get(1));
        // Add client node to controller's node map
        controller->AddToClientNodeMap(i, clientNode);
    }
    // Connect server to the switch
    for (uint32_t i = 0; i < server_num; i++)
    {
        int switchId = (client_num + i) % 2;
        Ptr<Node> serverNode = serverNodes.Get(i);
        Ptr<Node> switchNode = switchNodes.Get(switchId);
        NodeContainer pair(serverNode, switchNode);
        NetDeviceContainer link = csmaHelper.Install(pair);
        nodeDevices.Add(link.Get(0)); // Add server node's net device to the vector
        switchNodeDevices[switchId].Add(link.Get(1));
        // Add server node to controller's node map
        controller->AddToServerNodeMap(i, serverNode);
    }
    // Connect two switches
    NetDeviceContainer link = csmaHelper.Install(switchNodes);
    switchNodeDevices[0].Add(link.Get(0));
    switchNodeDevices[1].Add(link.Get(1));

    // Install the TCP/IP stack into all client/server nodes
    InternetStackHelper internet;
    internet.Install(clientNodes);
    internet.Install(serverNodes);
    // Set IPv4 address for all client/server nodes
    // All client/server nodes is in the same network which is different from the SDN controller
    Ipv4AddressHelper ipv4Helper;
    Ipv4InterfaceContainer ipIfaces;
    ipv4Helper.SetBase("10.1.1.0", "255.255.255.0");
    ipIfaces = ipv4Helper.Assign(nodeDevices);

    // Configure the OpenFlow network domain
    Ptr<OFSwitch13InternalHelper> of13Helper = CreateObject<OFSwitch13InternalHelper>();
    of13Helper->InstallController(controllerNode, controller); // Install SDN-KVS Controller
    of13Helper->InstallSwitch(switchNodes.Get(0), switchNodeDevices[0]);
    of13Helper->InstallSwitch(switchNodes.Get(1), switchNodeDevices[1]);
    of13Helper->CreateOpenFlowChannels();

    // Install server application
    ServerHelper serverHelper(server_port, server_rate, server_tqs, algorithm);
    serverHelper.Install(serverNodes);
    // Generate request table
    NS_LOG_UNCOND("===== Prebuild request table =====");
    init_request_table(serverNodes);
    // Install client application
    ClientHelper clientHelper(req_table, algorithm);
    clientHelper.Install(clientNodes);

    // Run the simulation
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}
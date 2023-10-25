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
bool debug = false;                           // Enable DEBUG log
bool ifZipfian = true;                        // Whether there is a hotspot server
DistributionType distribution_type = POSSION; // The distribution type for generating key
KeysetSizeType keyset_size_type = LIGHT;      // The keyset size type
double uti = 0.7;                             // Utilization
uint32_t simTime = 100;                       // Simulator running time(unit: second)
uint32_t server_rate = 1;     // Server handle rate. (Number of keys processed per millisecond)
uint16_t server_port = 25565; // Server listen port
uint32_t server_tqs = 64;     // Server TQS
uint32_t client_num = 5;      // The number of client node
uint32_t server_num = 5;      // The number of server node
uint32_t key_num = 100000;    // The upper bound of the total number of keys(n keys = 1 keyset)
uint32_t request_num = 100;   // The upper bound of the total number of requests

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
generate_size()
{
    uint32_t ret = 0;
    switch (distribution_type)
    {
    case POSSION: {
        Ptr<UniformRandomVariable> x = CreateObject<UniformRandomVariable>();
        x->SetAttribute("Min", DoubleValue(request_num - 5));
        x->SetAttribute("Max", DoubleValue(request_num + 5));
        ret = x->GetInteger();
        break;
    }
    case PARETO: {
        double rand_value = rand() / double(RAND_MAX);
        double alpha = 2.0;
        uint32_t x_min = uint32_t(request_num * (alpha - 1) / alpha);
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
            double mean = double(request_num / double(ks_num));
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
        x->SetAttribute("Min", DoubleValue(request_num / double(ks_num) - 3));
        x->SetAttribute("Max", DoubleValue(request_num / double(ks_num) + 3));
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
generate_dst_server(std::unordered_map<uint32_t, bool>& is_add, uint32_t& hot_server_num)
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
            if (x >= 101 && x <= 120)
                ++hot_server_num;
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
    pV = (-1.0 / lambda) * log(1 - pV);
    return pV;
}

/**
 * Prebuild request table
 * \param serverNodes Node container of server nodes. It is used to get IPv4 address from node.
 */
void
init_request_table(NodeContainer& serverNodes)
{
    std::vector<RequestTableEntry> vec; // Temporary vector to store entry
    for (uint32_t req_id = 1; req_id <= request_num; req_id++)
    {
        std::vector<uint32_t> keyset; // All keysets generated for this request

        // The client that this request was sent from
        uint32_t client_id = rand() % client_num;
        if (req_table.count(client_id) == 0)
            req_table[client_id] = std::vector<RequestTableEntry>();
        uint32_t key_siz = generate_size();  // The upper bound of the keyset size of this request
        uint32_t ks_num = generate_ks_num(); // The upper bound of the keyset number of this request

        // Generate keyset for request. The generated keyset's size and number will not be greater
        // than the upper bound.
        uint32_t iter_ks_siz = 0; // Each keyset size generated
        for (uint32_t i = key_siz, j = 0; i > 0 && j < ks_num; i -= iter_ks_siz, j++)
        {
            iter_ks_siz = generate_ks_siz(ks_num);
            if (iter_ks_siz > i)
                iter_ks_siz = i;
            keyset.push_back(iter_ks_siz);
        }
        // Count the total size and total number of keysets
        tot_key_num += std::accumulate(keyset.begin(), keyset.end(), 0);
        tot_ks_num += keyset.size();

        std::unordered_map<uint32_t, bool> is_add;
        uint32_t hot_server_num = 0;
        for (uint32_t ks_siz : keyset)
        {
            // The server that this keyset data packet was sent to
            uint32_t server_id = generate_dst_server(is_add, hot_server_num);
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

            // Record this entry to the temporary vector
            vec.push_back(entry);
        }

        if (keyset.size() == hot_server_num && keyset.size() > 1)
            NS_LOG_UNCOND("All are hotspot servers");

        key_num -= key_siz;
        ++tot_request_num; // Count the total number of requests
        if (key_num <= 0)
            break;
    }

    // Initialize send time
    double req_mean = 1.0 * tot_key_num / tot_ks_num;
    double send_time = 0;
    std::map<uint32_t, double> send_time_mp; // Map<request id, send time>
    for (uint32_t req_id = 1; req_id <= tot_request_num; req_id++)
    {
        double t_mean = (1 / (server_rate * 1000.0)) * req_mean / (uti * double(server_num));
        double r = 0;
        while (true)
        {
            r = poisson(1 / t_mean);
            if (r != 0)
                break;
        }
        double double_tNext = Seconds(r).GetSeconds();
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
               << ", req_id=" << entry.req_id << ", ks_siz=" << entry.ks_siz << ")";
            NS_LOG_UNCOND(ss.str());
        }
    }
}

int
main(int argc, char* argv[])
{
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

    // Use the CsmaHelper to connect client or server nodes to the switch node
    CsmaHelper csmaHelper;
    csmaHelper.SetChannelAttribute("DataRate", DataRateValue(DataRate("100Mbps")));
    csmaHelper.SetChannelAttribute("Delay", TimeValue(MilliSeconds(2)));

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
    ServerHelper serverHelper(server_port, server_rate, server_tqs);
    serverHelper.Install(serverNodes);
    // Generate request table
    NS_LOG_UNCOND("===== Prebuild request table =====");
    init_request_table(serverNodes);
    // Install client application
    ClientHelper clientHelper(req_table);
    clientHelper.Install(clientNodes);

    // Run the simulation
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}
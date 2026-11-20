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
#include <random>
#include <regex>
using namespace ns3;

NS_LOG_COMPONENT_DEFINE("SDN-KVS-MAIN");

/**
 * Parameter configure
 */
SchedulingType algorithm = SchedulingStrategy::FIFO;
bool debug = false;               // Enable DEBUG log
bool ifZipfian = true;            // Whether there is a hotspot server
bool req_table_from_file = false; // Whether get request table from request_table.txt file
DistributionType req_size_distribution_type = PARETO; // The distribution type for generating key
KeysetSizeType keyset_size_type = HEAVY;              // The keyset size type
SendTimeType send_time_type = DYNAMIC;                // The send time type
double uti = 0.7;        // Utilization(Always 1.0 when dynamic send time type)
uint32_t simTime = 1000; // Simulator running time(unit: second)
ServerRateRatio server_rate_ratio = {1, 0, 0}; // Server rate ratio. (Server rate: number of keys
                                               // processed per millisecond)
uint16_t server_port = 25565;                  // Server listen port
uint32_t server_tqs = 64;                      // Server TQS
uint32_t client_num = 15;                      // The number of client node
uint32_t server_num = 15;                      // The number of server node
uint32_t key_num = 50000; // The upper bound of the total number of keys(n keys = 1 keyset)
uint32_t f_tqs_dynamic = 1;
uint32_t network_latency = 600; // Default: 600ns

static const int request_size = 80;
/**
 * Global variable
 */
uint32_t tot_key_num = 0;     // The total number of keys(it is also the sum of keyset size)
uint32_t tot_request_num = 0; // The total number of requests
uint32_t tot_ks_num = 0;      // The total number of keyset
std::map<uint32_t, std::vector<RequestTableEntry>> req_table; // Key: client id
std::mt19937 gen(1701);
std::poisson_distribution<> req_siz_possion_rng(request_size);
Ptr<ParetoRandomVariable> req_siz_rng = CreateObject<ParetoRandomVariable>();
uint32_t cur_heavy_dst_server = 0;

/**
 * Generate destination server id(start from 0) for keyset
 */
uint32_t
generate_dst_server(std::unordered_map<uint32_t, bool>& is_add,
                    Ptr<RandomVariableStream> rng,
                    bool is_heavy)
{
    uint32_t ret;
    if (is_heavy)
    {
        ret = cur_heavy_dst_server;
        is_add[ret] = true;
        cur_heavy_dst_server = (cur_heavy_dst_server + 1) % server_num;
    }
    else
    {
        while (true)
        {
            uint32_t x = rng->GetInteger() - 1; // [0, server_num - 1]
            if (ifZipfian)
            {
                int value1 = server_num * 0.2 < 1 ? 1 : server_num * 0.2 + 1;
                // 生成 value1 * (value - 1) 到 value1 * x 之间的随机数
                int value2 = (rand() % (value1 + 1)) + value1 * (x - 1);
                x = value2 % server_num;
            }

            if (is_add[x] == false)
            {
                ret = x;
                is_add[x] = true;
                break;
            }
        }
    }
    return ret; // Start from 0
}

uint32_t
generate_request_size()
{
    uint32_t x = 0;
    switch (req_size_distribution_type)
    {
    case POSSION: {
        x = req_siz_possion_rng(gen); // modify by wk
        break;
    }
    case PARETO: {
        x = req_siz_rng->GetInteger();
        break;
    }
    }
    return x;
}

/**
 * Print the request table
 */
void
print_request_table()
{
    std::cout << "tot_key_num = " << tot_key_num << ", tot_request_num = " << tot_request_num
              << ", tot_ks_num = " << tot_ks_num << std::endl;
    for (const auto& [client_id, entry_vec] : req_table)
    {
        for (const auto& entry : entry_vec)
        {
            std::cout << "[" << client_id << "] (id=" << entry.id << ", st=" << entry.st
                      << ", src_client=" << entry.src_client << ", dst_server=" << entry.dst_server
                      << ", dst_ip=" << entry.dst_ip << ", req_id=" << entry.req_id
                      << ", ks_siz=" << entry.ks_siz
                      << ", bottleneck_ks_size=" << entry.bottleneck_ks_size
                      << ", is_heavy=" << entry.is_heavy << ")" << std::endl;
        }
    }
}

/**
 * Generate the random number
 *
 * \param min Min number
 * \param max Max number
 * \param n The number count
 */
std::vector<uint32_t>
generate_uniform_random_number(uint32_t min, uint32_t max, uint32_t n)
{
    std::vector<uint32_t> ret;
    if (n <= 0)
        return ret;
    Ptr<UniformRandomVariable> rng = CreateObject<UniformRandomVariable>();
    rng->SetAttribute("Min", DoubleValue(min));
    rng->SetAttribute("Max", DoubleValue(max));
    std::vector<uint32_t> random_vec;
    random_vec.push_back(min);
    random_vec.push_back(max);
    for (uint32_t i = 1; i <= n - 1; i++)
        random_vec.push_back(rng->GetInteger());
    std::sort(random_vec.begin(), random_vec.end());
    for (uint32_t i = 1; i < random_vec.size(); i++)
    {
        uint32_t val = random_vec[i] - random_vec[i - 1];
        ret.push_back(val);
    }
    return ret;
}

/**
 * Build request table from file
 */
void
init_request_table_from_file()
{
    std::ifstream read_file;
    read_file.open("/home/ren/wck/SDN-KVS/request_table.txt", std::ios::in);
    if (read_file.is_open())
    {
        std::string str;
        while (getline(read_file, str))
        {
            std::regex pattern(
                "\\[[0-9]+\\] \\(id=(\\d+), st=(.*?), src_client=(\\d+), "
                "dst_server=(\\d+), dst_ip=(\\d+\\.\\d+\\.\\d+\\.\\d+), req_id=(\\d+), "
                "ks_siz=(\\d+), bottleneck_ks_size=(\\d+), is_heavy=(\\d+)\\)");

            std::smatch result;
            if (std::regex_search(str, result, pattern))
            {
                RequestTableEntry entry;
                entry.id = std::stoul(result[1].str());
                entry.st = std::stod(result[2].str());
                entry.src_client = std::stoul(result[3].str());
                entry.dst_server = std::stoul(result[4].str());
                entry.dst_ip = result[5].str();
                entry.req_id = std::stoul(result[6].str());
                entry.ks_siz = std::stoul(result[7].str());
                entry.bottleneck_ks_size = std::stoul(result[8].str());
                entry.is_heavy = std::stoul(result[9].str());
                entry.dst_port = server_port;
                tot_key_num += entry.ks_siz;
                tot_ks_num++;
                tot_request_num = std::max(tot_request_num, entry.req_id);
                req_table[entry.src_client].push_back(entry);
            }
        }
    }
    else
    {
        std::cout << "Cannot open file" << std::endl;
    }
}

/**
 * Prebuild request table
 * \param serverNodes Node container of server nodes. It is used to get IPv4 address from node.
 */
void
init_request_table(NodeContainer& serverNodes)
{
    Ptr<UniformRandomVariable> client_rng = CreateObject<UniformRandomVariable>();
    client_rng->SetAttribute("Min", DoubleValue(0));
    client_rng->SetAttribute("Max", DoubleValue(client_num - 1));

    double alpha = 2.0;
    double x_min = request_size * (alpha - 1) / alpha;
    req_siz_rng->SetAttribute("Scale", DoubleValue(x_min));
    req_siz_rng->SetAttribute("Shape", DoubleValue(alpha));
    req_siz_rng->SetAttribute("Bound", DoubleValue(request_size));

    std::poisson_distribution<> ks_num_rng(server_num / 2.0); // modify by wangkai

    Ptr<RandomVariableStream> server_rng;
    if (ifZipfian)
    {
        server_rng = CreateObject<ZipfRandomVariable>();
        server_rng->SetAttribute("N", IntegerValue(server_num));
        server_rng->SetAttribute("Alpha", DoubleValue(1));
    }
    else
    {
        server_rng = CreateObject<UniformRandomVariable>();
        server_rng->SetAttribute("Min", DoubleValue(1));
        server_rng->SetAttribute("Max", DoubleValue(server_num));
    }

    std::vector<RequestTableEntry> vec; // Temporary vector to store entry
    uint32_t req_id = 1;
    while (key_num > 0)
    {
        std::vector<uint32_t> keyset; // All keysets generated for this request
        std::vector<bool> keyset_heavy_flag;

        // The client that this request was sent from
        uint32_t client_id = client_rng->GetInteger();
        if (req_table.count(client_id) == 0)
            req_table[client_id] = std::vector<RequestTableEntry>();
        // The upper bound of request size
        uint32_t req_siz = generate_request_size();
        // std::cout << "debug req size " << req_siz << std::endl;
        req_siz = req_siz > key_num ? key_num : req_siz;
        // The upper bound of the keyset number of this request
        uint32_t ks_num = ks_num_rng(gen);
        if (ks_num < 2)
            ks_num = 2;
        if (ks_num > server_num)
            ks_num = server_num;

        // Generate keyset for request. The generated keyset's size and number will not be
        // greater than the upper bound.
        switch (keyset_size_type)
        {
        case HEAVY: {
            uint32_t heavy_siz = uint32_t(ceil(req_siz * 0.8));
            uint32_t heavy_cnt = uint32_t(ceil(ks_num * 0.2));
            uint32_t light_siz = req_siz - heavy_siz;
            uint32_t light_cnt = ks_num - heavy_cnt;
            std::vector<uint32_t> heavy_vec =
                generate_uniform_random_number(0, heavy_siz, heavy_cnt);
            std::vector<uint32_t> light_vec =
                generate_uniform_random_number(0, light_siz, light_cnt);
            for (auto val : heavy_vec)
            {
                if (val == 0)
                    continue;
                // std::cout << "debug keyset size " << val << std::endl;
                keyset.push_back(val);
                keyset_heavy_flag.push_back(true);
            }
            for (auto val : light_vec)
            {
                if (val == 0)
                    continue;
                // std::cout << "debug keyset size " << val << std::endl;
                keyset.push_back(val);
                keyset_heavy_flag.push_back(false);
            }
            break;
        }
        case LIGHT: {
            std::vector<uint32_t> random_vec = generate_uniform_random_number(0, req_siz, ks_num);
            for (auto val : random_vec)
            {
                if (val == 0)
                    continue;
                // std::cout << "debug keyset size " << val << std::endl;
                keyset.push_back(val);
                keyset_heavy_flag.push_back(false);
            }
            break;
        }
        }

        // Count the total size and total number of keysets
        ks_num = keyset.size();
        tot_key_num += std::accumulate(keyset.begin(), keyset.end(), 0);
        tot_ks_num += keyset.size();
        uint32_t bottleneck_keyset =
            *std::max_element(keyset.begin(), keyset.end()); // bottleneck keyset size
        std::unordered_map<uint32_t, bool> is_add;
        for (uint32_t i = 0; i < keyset.size(); i++)
        {
            uint32_t ks_siz = keyset[i];
            bool is_heavy = keyset_heavy_flag[i];
            // The server that this keyset data packet was sent to
            uint32_t server_id = generate_dst_server(is_add, server_rng, is_heavy);
            // Get IPv4 address from server id
            Ptr<Node> serverNode = serverNodes.Get(server_id);
            std::stringstream ss;
            ss << serverNode->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();

            // Build request table entry
            RequestTableEntry entry;
            entry.id = 0;   // Initialize it later
            entry.st = 0.0; // Initialize it later
            entry.src_client = client_id;
            entry.dst_server = server_id;
            entry.dst_ip = ss.str();
            entry.dst_port = server_port;
            entry.req_id = req_id;
            entry.ks_siz = ks_siz * 10; // modify by wangkai 小规模场景（8~13均设置为*1）
            entry.bottleneck_ks_size = bottleneck_keyset * 10; // modify by wangkai 小规模场景（8~13均设置为*1）
            entry.is_heavy = is_heavy;

            // Record this entry to the temporary vector
            vec.push_back(entry);
        }

        key_num -= req_siz;
        ++tot_request_num; // Count the total number of requests
        req_id++;
    }

    // Initialize send time
    // double req_mean = 1.0 * tot_key_num / tot_ks_num;
    double req_mean = 1.0 * tot_key_num * 1 / tot_request_num;
    double send_time = 2;
    uint32_t ratio_tot = server_rate_ratio.r1 + server_rate_ratio.r2 + server_rate_ratio.r3;
    double server_rate =
        1.0 * (server_rate_ratio.r1 + server_rate_ratio.r2 * 2 + server_rate_ratio.r3 * 3) /
        ratio_tot;
    double t_mean = (req_mean * 1.0 / (server_rate * 1000.0)) / (uti * double(server_num));
    // tot_key_num = 150000, tot_request_num = 1254, tot_ks_num = 27886, avg_req_size = 119.6172,
    // avg_ks_size = 5.3790

    //  req_mean = 150000 / 1254 = 119.62
    //  t_mean = 120 / (1000 /s *0.7 * 50) = 3.43*1e-3 s = 3.43 ms

    std::map<uint32_t, double> send_time_mp; // Map<request id, send time>

    // For dynamic send_time_type
    Ptr<ExponentialRandomVariable> send_time_rng = CreateObject<ExponentialRandomVariable>();
    send_time_rng->SetAttribute("Mean", DoubleValue(t_mean));
    // For static send_time_type
    double dt = 0.00001;
    double delta_time = 0.01;
    uint32_t groups_num = std::min(50U, std::max(1U, tot_request_num / 5)); // [1, 50]
    uint32_t dt_base = 0;

    for (uint32_t req_id = 1; req_id <= tot_request_num; req_id++)
    {
        switch (send_time_type)
        {
        case STATIC: {
            send_time_mp[req_id] = send_time + dt * dt_base;
            ++dt_base;
            if (req_id % groups_num == 0)
            {
                dt_base = 0;
                send_time += delta_time;
            }
            break;
        }
        case DYNAMIC:
        default: {
            double r = 0;
            while (true)
            {
                r = send_time_rng->GetValue();
                if (r != 0)
                    break;
            }
            send_time += Seconds(r).GetSeconds();
            send_time_mp[req_id] = send_time;
        }
        }
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

    // Initialize keyset ID
    uint32_t id_cnt = 0;
    for (auto& [client_id, vec] : req_table)
        for (auto& entry : vec)
            entry.id = ++id_cnt;
}

ServerRateRatio
parse_ratio(std::string ratio_str)
{
    size_t pos = 0;
    std::string token;
    std::vector<uint32_t> ratio_vec;
    try
    {
        while ((pos = ratio_str.find(":")) != std::string::npos)
        {
            token = ratio_str.substr(0, pos);
            ratio_vec.push_back(std::stoul(token));
            ratio_str.erase(0, pos + 1);
        }
        ratio_vec.push_back(std::stoul(ratio_str));
        while (ratio_vec.size() < 3)
            ratio_vec.push_back(0);
    }
    catch (const std::exception& e)
    {
        std::cout << "Failed to parse server rate ratio, use default value(Default: 1:0:0)"
                  << std::endl;
        ratio_vec.clear();
        ratio_vec.push_back(1);
        ratio_vec.push_back(0);
        ratio_vec.push_back(0);
    }
    // Build server rate ratio struct variable
    // Assert that the size of ratio_vec is always >= 3
    ServerRateRatio ans{ratio_vec[0], ratio_vec[1], ratio_vec[2]};
    // Simplify the ratio
    uint32_t gcd = std::gcd(std::gcd(ans.r1, ans.r2), ans.r3);
    if (gcd != 0)
    {
        ans.r1 /= gcd, ans.r2 /= gcd, ans.r3 /= gcd;
    }
    else if (ans.r1 != 0 && ans.r2 == 0 && ans.r3 == 0)
    {
        ans.r1 = 1;
    }
    else if (ans.r1 == 0 && ans.r2 != 0 && ans.r3 == 0)
    {
        ans.r2 = 1;
    }
    else if (ans.r1 == 0 && ans.r2 == 0 && ans.r3 != 0)
    {
        ans.r3 = 1;
    }
    else
    {
        uint32_t gcd1 = std::gcd(ans.r1, ans.r2);
        uint32_t gcd2 = std::gcd(ans.r1, ans.r3);
        uint32_t gcd3 = std::gcd(ans.r2, ans.r3);
        gcd = (gcd1 == 0) ? ((gcd2 == 0) ? gcd3 : gcd2) : gcd1;
        ans.r1 /= gcd, ans.r2 /= gcd, ans.r3 /= gcd;
    }
    // Check if the server can just prorate the server rate.
    // If not, fall back to the default ratio.
    uint32_t ratio_tot = ans.r1 + ans.r2 + ans.r3;
    bool flag1 = (ans.r1 == 0) ? true : ((server_num * ans.r1) % ratio_tot) == 0;
    bool flag2 = (ans.r2 == 0) ? true : ((server_num * ans.r2) % ratio_tot) == 0;
    bool flag3 = (ans.r3 == 0) ? true : ((server_num * ans.r3) % ratio_tot) == 0;
    if (!(flag1 && flag2 && flag3))
    {
        std::cout
            << "The server cannot allocate rates exactly proportionally. Fall back to default "
               "proportions.(Default: 1:0:0)"
            << std::endl;
        ans.r1 = 1, ans.r2 = 0, ans.r3 = 0;
    }
    return ans;
}

void
init_parameter(int argc, char* argv[])
{
    CommandLine cmd;
    uint32_t algorithm_index = 0;                // Default: FIFO
    uint32_t distribution_type_index = 0;        // Default: POSSION
    uint32_t keyset_size_type_index = 0;         // Default: LIGHT
    uint32_t send_time_type_index = 0;           // Default: DYNAMIC
    std::string server_rate_ratio_str = "1:0:0"; // Default: 1:0:0
    cmd.AddValue("algorithm",
                 "Algorithm(0: FIFO, 1: SRPT, 2: SBF, 3: DAS, 4: SDN_KVS)",
                 algorithm_index);
    cmd.AddValue("debug", "Enable DEBUG log", debug);
    cmd.AddValue("ifZipfian", "Whether there is a hotspot server", ifZipfian);
    cmd.AddValue("distribution_type",
                 "The distribution type for generating key(0: POSSION, 1: PARETO)",
                 distribution_type_index);
    cmd.AddValue("keyset_size_type",
                 "The keyset size type(0: LIGHT, 1: HEAVY)",
                 keyset_size_type_index);
    cmd.AddValue("send_time_type",
                 "The send time type(0: DYNAMIC, 1: STATIC)",
                 send_time_type_index);
    cmd.AddValue("uti", "Utilization", uti);
    cmd.AddValue("simTime", "Simulator running time(unit: second)", simTime);
    cmd.AddValue("server_rate_ratio",
                 "Server rate ratio. (Server rate: number of keys processed per millisecond)",
                 server_rate_ratio_str);
    cmd.AddValue("server_tqs", "Server TQS", server_tqs);
    cmd.AddValue("client_num", "The number of client node", client_num);
    cmd.AddValue("server_num", "The number of server node", server_num);
    cmd.AddValue("key_num",
                 "The upper bound of the total number of keys(n keys = 1 keyset)",
                 key_num);
    cmd.AddValue("tqs_dynamic", "", f_tqs_dynamic);
    cmd.AddValue("network_latency", "", network_latency);

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
        keyset_size_type = HEAVY;
        break;
    case 0:
    default:
        keyset_size_type = LIGHT;
    }

    switch (send_time_type_index)
    {
    case 1:
        send_time_type = STATIC;
        uti = 1.0;
        break;
    case 0:
    default:
        send_time_type = DYNAMIC;
    }

    // Parse server rate ratio string
    server_rate_ratio = parse_ratio(server_rate_ratio_str);

    // Print the parameter
    std::cout << "===== Parameter configure =====" << std::endl;
    std::cout << " - algorithm = " << algorithm << std::endl;
    std::cout << " - debug = " << debug << std::endl;
    std::cout << " - ifZipfian = " << ifZipfian << std::endl;
    std::cout << " - distribution_type = " << req_size_distribution_type << std::endl;
    std::cout << " - keyset_size_type = " << keyset_size_type << std::endl;
    std::cout << " - send_time_type = " << send_time_type << std::endl;
    std::cout << " - uti = " << uti << std::endl;
    std::cout << " - simTime = " << simTime << std::endl;
    std::cout << " - server_rate_ratio = " << server_rate_ratio << std::endl;
    std::cout << " - server_tqs = " << server_tqs << std::endl;
    std::cout << " - client_num = " << client_num << std::endl;
    std::cout << " - server_num = " << server_num << std::endl;
    std::cout << " - key_num = " << key_num << std::endl;
    std::cout << " - request_size = " << request_size << std::endl;
}

int
main(int argc, char* argv[])
{
    init_parameter(argc, argv);

    // IMPORTANT: CSMA will drop some packets without this line
    Config::SetDefault("ns3::ArpCache::PendingQueueSize", UintegerValue(10000));
    Config::SetDefault("ns3::ArpCache::WaitReplyTimeout", TimeValue(Seconds(10)));
    Config::SetDefault("ns3::ArpCache::MaxRetries", UintegerValue(300));
    // Config::SetDefault("ns3::CsmaNetDevice::TxQueue::MaxPackets", UintegerValue(10000));

    // Config::SetDefault("ns3::QueueBase::MaxPackets", UintegerValue(10000000));

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
    controller->SetDebug(debug);
    // Use the CsmaHelper to connect client or server nodes to the switch node
    CsmaHelper csmaHelper;
    csmaHelper.SetChannelAttribute("DataRate", DataRateValue(DataRate("100Gbps")));
    // csmaHelper.SetChannelAttribute("Delay", TimeValue(PicoSeconds(1))); //// wk
    // csmaHelper.SetChannelAttribute("Delay", TimeValue(NanoSeconds(600))); //// wk
    csmaHelper.SetChannelAttribute("Delay", TimeValue(NanoSeconds(network_latency))); //// wk
    /*
    [Server][+2.01623s] Server 46(queue_siz=0, update_time=+0s) received SDN-KVS packet(id=25381,
    req_id=3, ks_siz=4, bottleneck_ks_siz=0, queue_siz=0, server_rate=0, heavy=0, server_tqs=0,
    priority=4) from 10.1.1.46 port 49180

    [Server][+6.55658s] Server 44(queue_siz=0, update_time=+0s) sent response packet(id=22017,
    req_id=1254, ks_siz=1, bottleneck_ks_siz=0, queue_siz=0, server_rate=1, heavy=0, server_tqs=6,
    priority=1) to 10.1.1.39 port 49797

    [Client][+2.01667s] Received SDN-KVS packet(id=11435, req_id=1, ks_siz=5, bottleneck_ks_siz=0,
    queue_siz=0, server_rate=1, heavy=0, server_tqs=6, priority=5) from 10.1.1.70 port 25565


    */

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

    double SR_avg = 1.0;

    if ((server_rate_ratio.r1 == 1 && server_rate_ratio.r2 == 0 && server_rate_ratio.r3 == 0))
    {
        SR_avg = 1.0;
    }
    else if ((server_rate_ratio.r1 == 1 && server_rate_ratio.r2 == 1 && server_rate_ratio.r3 == 0))
    {
        SR_avg = (1.0 + 2.0) / 2;
    }
    else if ((server_rate_ratio.r1 == 5 && server_rate_ratio.r2 == 3 && server_rate_ratio.r3 == 2))
    {
        SR_avg = (1.0 * 5 + 2.0 * 3 + 1.0 * 2) / 10;
    }

    ServerHelper serverHelper(server_port,
                              server_rate_ratio,
                              server_tqs,
                              algorithm,
                              f_tqs_dynamic,
                              SR_avg);
    ApplicationContainer server_apps = serverHelper.Install(serverNodes);
    // Print server information log
    std::cout << "==== Server information ====" << std::endl;
    for (auto i = server_apps.Begin(); i != server_apps.End(); ++i)
    {
        Ptr<Application> app = *i;
        UintegerValue id, server_rate, tqs;
        BooleanValue srpt_enabled, sbf_enabled;
        app->GetAttribute("ServerId", id);
        app->GetAttribute("ServerRate", server_rate);
        app->GetAttribute("TQS", tqs);
        app->GetAttribute("SrptEnabled", srpt_enabled);
        app->GetAttribute("SbfEnabled", sbf_enabled);
        std::cout << "[" << id.Get() << "] (server_id=" << id.Get()
                  << ", server_rate=" << server_rate.Get() << ", tqs=" << tqs.Get()
                  << ", srpt_enabled=" << srpt_enabled.Get()
                  << ", sbf_enabled=" << sbf_enabled.Get() << ")" << std::endl;
    }
    // Generate request table
    std::cout << "===== Prebuild request table =====" << std::endl;
    if (req_table_from_file)
        init_request_table_from_file();
    else
        init_request_table(serverNodes);
    print_request_table();
    // Install client application
    ClientHelper clientHelper(req_table, algorithm, debug);
    clientHelper.Install(clientNodes);

    // Run the simulation
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}
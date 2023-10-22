#include "controller.h"
#include "server-helper.h"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-apps-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/ofswitch13-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("SDN-KVS-MAIN");

bool debug = true;
uint32_t client_num = 3;
uint32_t server_num = 3;

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
        LogComponentEnable("Controller", LOG_LEVEL_ALL);
        LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_ALL);
        LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_ALL);
        LogComponentEnable("ServerApplication", LOG_LEVEL_ALL);
    }

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
    ServerHelper serverHelper(25565);
    serverHelper.Install(serverNodes);
    // TODO: Generate request table
    // TODO: Install client application
    for (uint32_t i = 0; i < server_num; i++)
    {
        // assert that client_num == server_num
        Ptr<Node> serverNode = serverNodes.Get(i);
        Ptr<Ipv4> ipp = serverNode->GetObject<Ipv4>();
        Address target = ipp->GetAddress(1, 0).GetLocal().ConvertTo();
        UdpEchoClientHelper udpClientHelper(target, 25565);
        // udpClientHelper.SetAttribute("MaxPackets", UintegerValue(2));
        udpClientHelper.Install(clientNodes.Get(i));
    }

    // Run the simulation
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}
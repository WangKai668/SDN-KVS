#ifndef MODEL_H
#define MODEL_H

#include <string>

/**
 * Request table entry. Each client holds a request table that records the sending time and related
 * information of each data packet.
 */
struct RequestTableEntry
{
    uint32_t id;         // Keyset ID(for debug)
    double st;           // Send time (keysets from the same request are equal)
    uint32_t src_client; // Source client id(start from 0)
    uint32_t dst_server; // Destination server id(start from 0)
    std::string dst_ip;  // Destination server IPv4 address string
    uint16_t dst_port;   // Destination server port
    uint32_t req_id;     // Request id that the packet belongs to
    uint32_t ks_siz;     // Keyset size
    uint32_t bottleneck_ks_size;
};

enum DistributionType
{
    POSSION, // Possion distribution
    PARETO   // Pareto distribution
};

enum KeysetSizeType
{
    HEAVY, // Heavy
    LIGHT  // Light
};

enum class ServerSchedulingType
{
    SRPT,
    SBF,
    NONE // No scheduling. FIFO
};

enum class ClientSchedulingType
{
    DAS,
    SBF,
    NONE // No Scheduling. Sent according to request table.
};

enum class ControllerSchedulingType
{
    SDN_KVS,
    NONE // No scheduling. No packets will be forwarded to the controller
};

struct SchedulingType
{
    ServerSchedulingType server_algorithm;
    ClientSchedulingType client_algorithm;
    ControllerSchedulingType controller_algorithm;
    std::string name;
};

inline std::ostream&
operator<<(std::ostream& os, DistributionType c)
{
    switch (c)
    {
    case PARETO:
        os << "PARETO";
        break;
    case POSSION:
    default:
        os << "POSSION";
    }
    return os;
}

inline std::ostream&
operator<<(std::ostream& os, KeysetSizeType c)
{
    switch (c)
    {
    case LIGHT:
        os << "LIGHT";
        break;
    case HEAVY:
    default:
        os << "HEAVY";
    }
    return os;
}

inline std::ostream&
operator<<(std::ostream& os, ServerSchedulingType c)
{
    switch (c)
    {
    case ServerSchedulingType::SRPT:
        os << "SRPT";
        break;
    case ServerSchedulingType::SBF:
        os << "SBF";
        break;
    case ServerSchedulingType::NONE:
    default:
        os << "NONE";
    }
    return os;
}

inline std::ostream&
operator<<(std::ostream& os, ClientSchedulingType c)
{
    switch (c)
    {
    case ClientSchedulingType::DAS:
        os << "DAS";
        break;
    case ClientSchedulingType::SBF:
        os << "SBF";
        break;
    case ClientSchedulingType::NONE:
    default:
        os << "NONE";
    }
    return os;
}

inline std::ostream&
operator<<(std::ostream& os, ControllerSchedulingType c)
{
    switch (c)
    {
    case ControllerSchedulingType::SDN_KVS:
        os << "SDN_KVS";
        break;
    case ControllerSchedulingType::NONE:
    default:
        os << "NONE";
    }
    return os;
}

inline std::ostream&
operator<<(std::ostream& os, SchedulingType type)
{
    os << type.name << "(server_algorithm: " << type.server_algorithm
       << ", client_algorithm: " << type.client_algorithm
       << ", controller_algorithm: " << type.controller_algorithm << ")";
    return os;
}

namespace SchedulingStrategy

{
__attribute__((unused)) static SchedulingType FIFO = {ServerSchedulingType::NONE,
                                                      ClientSchedulingType::NONE,
                                                      ControllerSchedulingType::NONE,
                                                      "FIFO"};
__attribute__((unused)) static SchedulingType SRPT = {ServerSchedulingType::SRPT,
                                                      ClientSchedulingType::NONE,
                                                      ControllerSchedulingType::NONE,
                                                      "SRPT"};
__attribute__((unused)) static SchedulingType SBF = {ServerSchedulingType::SBF,
                                                     ClientSchedulingType::SBF,
                                                     ControllerSchedulingType::NONE,
                                                     "SBF"};
__attribute__((unused)) static SchedulingType DAS = {ServerSchedulingType::SRPT,
                                                     ClientSchedulingType::DAS,
                                                     ControllerSchedulingType::NONE,
                                                     "DAS"};
__attribute__((unused)) static SchedulingType SDN_KVS = {ServerSchedulingType::NONE,
                                                         ClientSchedulingType::NONE,
                                                         ControllerSchedulingType::SDN_KVS,
                                                         "SDN-KVS"};
} // namespace SchedulingStrategy
#endif
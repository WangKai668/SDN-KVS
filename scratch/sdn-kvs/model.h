#ifndef MODEL_H
#define MODEL_H

#include <string>

/**
 * Request table entry. Each client holds a request table that records the sending time and related
 * information of each data packet.
 */
struct RequestTableEntry
{
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

enum SchedulingType
{
    FIFO,
    SRPT,
    SBF,
    SDN_KVS,
    DAS
};
#endif
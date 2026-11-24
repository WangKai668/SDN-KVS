#ifndef SDN_KVS_CONTROLLER_OVERHEAD_H
#define SDN_KVS_CONTROLLER_OVERHEAD_H

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace ns3
{

struct ControllerOverhead
{
    enum class FlowUpdate
    {
        None,
        Redirect,
        Restore
    };

    inline static bool enabled = false;
    inline static bool trace = false;
    inline static uint64_t calls = 0;
    inline static std::chrono::steady_clock::duration duration{};
    inline static uint64_t overload = 0;
    inline static uint64_t redirect = 0;
    inline static uint64_t restore = 0;

    static void Record(uint64_t& count, const char* kind)
    {
        if (enabled)
        {
            ++count;
            if (trace)
            {
                std::cout << "CONTROLLER_OVERHEAD_EVENT " << kind << '\n';
            }
        }
    }
};

} // namespace ns3

#endif

#pragma once

#include <windows.h>
#include <string>
#include <vector>

namespace ProcessConnections
{
    struct Connection
    {
        std::wstring localAddress;
        std::wstring remoteAddress;
        std::wstring state;
        unsigned short localPort{};
        unsigned short remotePort{};
        bool ipv6{};
    };

    struct Snapshot
    {
        std::vector<Connection> connections;
        // A nonzero error can accompany partial results from the other family.
        DWORD error{};
        bool truncated{};
    };

    // Reads current TCP endpoints using the caller's existing token. Numeric
    // addresses only: no DNS, network requests, disk writes or elevation.
    // UDP owner tables contain no remote endpoint, so they are not included.
    Snapshot Read(DWORD pid);
}

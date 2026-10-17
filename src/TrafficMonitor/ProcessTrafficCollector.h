#pragma once

#include <windows.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Counts TCP/UDP ETW packet bytes; it does not use process I/O counters.
// Start uses the caller's token and never requests elevation.
class CProcessTrafficCollector
{
public:
    struct Row
    {
        std::wstring name;
        DWORD pid{};
        std::uint64_t uploadBytesPerSecond{};
        std::uint64_t downloadBytesPerSecond{};
        std::uint64_t totalBytes{};
    };

    struct Snapshot
    {
        std::vector<Row> rows;
        std::uint64_t uploadBytesPerSecond{};
        std::uint64_t downloadBytesPerSecond{};
        bool available{};
        // Sticky until the next successful Start, also true when unavailable.
        bool dataLost{};
    };

    CProcessTrafficCollector();
    ~CProcessTrafficCollector();
    CProcessTrafficCollector(const CProcessTrafficCollector&) = delete;
    CProcessTrafficCollector& operator=(const CProcessTrafficCollector&) = delete;

    DWORD Start();
    void Stop();
    // Consumes the interval counters; one caller should own the sampling loop.
    Snapshot TakeSnapshot();

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

using ProcessTrafficCollector = CProcessTrafficCollector;

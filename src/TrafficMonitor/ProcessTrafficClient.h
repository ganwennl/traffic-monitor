#pragma once

#include "ProcessTrafficCollector.h"
#include <memory>
#include <mutex>

// The normal application keeps its own token. Only the explicitly requested
// collector process may be elevated, and it exposes no commands over IPC.
class CProcessTrafficClient
{
public:
    using Row = CProcessTrafficCollector::Row;

    struct Snapshot
    {
        std::vector<Row> rows;
        uint64_t uploadBytesPerSecond{};
        uint64_t downloadBytesPerSecond{};
        bool dataLost{};
        std::wstring status{ L"进程流量采集尚未启用" };
        bool canEnable{ true };
    };

    CProcessTrafficClient() = default;
    ~CProcessTrafficClient();
    CProcessTrafficClient(const CProcessTrafficClient&) = delete;
    CProcessTrafficClient& operator=(const CProcessTrafficClient&) = delete;

    void Start(HWND owner, bool allowElevation = true);
    void Stop();
    Snapshot TakeSnapshot() const;

    // Call before configuration, plugin, mutex or window initialization.
    // Returns true for the helper command, including malformed helper arguments.
    static bool IsCollectorCommandLine();
    static bool RunCollectorCommandLine(int& exitCode);

private:
    struct State;
    static unsigned __stdcall WorkerThread(void* context);
    static void Worker(const std::shared_ptr<State>& state);

    mutable std::mutex m_stateMutex;
    std::shared_ptr<State> m_state;
};

using ProcessTrafficClient = CProcessTrafficClient;

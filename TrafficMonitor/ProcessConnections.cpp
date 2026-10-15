#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include "ProcessConnections.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <new>
#include <set>
#include <tuple>
#include <utility>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace
{
    constexpr DWORD MaximumTableBytes = 32U * 1024U * 1024U;
    constexpr size_t MaximumConnections = 2048;
    constexpr unsigned MaximumTableAttempts = 5;

    struct ConnectionLess
    {
        bool operator()(const ProcessConnections::Connection& left, const ProcessConnections::Connection& right) const
        {
            return std::tie(left.remoteAddress, left.remotePort, left.state, left.ipv6, left.localAddress, left.localPort)
                < std::tie(right.remoteAddress, right.remotePort, right.state, right.ipv6, right.localAddress, right.localPort);
        }
    };

    using Connections = std::set<ProcessConnections::Connection, ConnectionLess>;

    DWORD ReadTable(ULONG family, std::vector<BYTE>& storage)
    {
        DWORD required{};
        DWORD error = ::GetExtendedTcpTable(nullptr, &required, FALSE, family, TCP_TABLE_OWNER_PID_ALL, 0);
        if (error != ERROR_INSUFFICIENT_BUFFER && error != ERROR_SUCCESS)
            return error;
        for (unsigned attempt = 0; attempt < MaximumTableAttempts; ++attempt)
        {
            if (required < sizeof(DWORD))
                return ERROR_INVALID_DATA;
            if (required > MaximumTableBytes)
                return ERROR_NOT_ENOUGH_MEMORY;
            storage.resize(required);
            DWORD supplied = static_cast<DWORD>(storage.size());
            error = ::GetExtendedTcpTable(storage.data(), &supplied, FALSE, family, TCP_TABLE_OWNER_PID_ALL, 0);
            if (error == ERROR_SUCCESS)
            {
                if (supplied > storage.size() || supplied < sizeof(DWORD))
                    return ERROR_INVALID_DATA;
                storage.resize(supplied);
                return ERROR_SUCCESS;
            }
            if (error != ERROR_INSUFFICIENT_BUFFER)
                return error;
            // Connections can be created between the size query and the read.
            // Grow even if a racing query reports the previous buffer size.
            if (supplied > storage.size())
                required = supplied;
            else if (storage.size() <= MaximumTableBytes - 4096U)
                required = static_cast<DWORD>(storage.size()) + 4096U;
            else
                return ERROR_NOT_ENOUGH_MEMORY;
        }
        return ERROR_INSUFFICIENT_BUFFER;
    }

    std::wstring StateName(DWORD state)
    {
        switch (state)
        {
        case MIB_TCP_STATE_CLOSED: return L"已关闭";
        case MIB_TCP_STATE_LISTEN: return L"监听";
        case MIB_TCP_STATE_SYN_SENT: return L"连接中";
        case MIB_TCP_STATE_SYN_RCVD: return L"等待确认";
        case MIB_TCP_STATE_ESTAB: return L"已连接";
        case MIB_TCP_STATE_FIN_WAIT1: return L"等待关闭 1";
        case MIB_TCP_STATE_FIN_WAIT2: return L"等待关闭 2";
        case MIB_TCP_STATE_CLOSE_WAIT: return L"对端已关闭";
        case MIB_TCP_STATE_CLOSING: return L"关闭中";
        case MIB_TCP_STATE_LAST_ACK: return L"等待最后确认";
        case MIB_TCP_STATE_TIME_WAIT: return L"等待回收";
        case MIB_TCP_STATE_DELETE_TCB: return L"正在回收";
        default: return L"未知（" + std::to_wstring(state) + L"）";
        }
    }

    DWORD AddressText(int family, const void* address, DWORD scope, std::wstring& text)
    {
        wchar_t buffer[INET6_ADDRSTRLEN]{};
        if (!::InetNtopW(family, address, buffer, _countof(buffer)))
            return static_cast<DWORD>(::WSAGetLastError());
        text = buffer;
        if (family == AF_INET6 && scope)
            text += L"%" + std::to_wstring(::ntohl(scope));
        return ERROR_SUCCESS;
    }

    void AddConnection(Connections& connections, ProcessConnections::Connection connection, bool& truncated)
    {
        connections.insert(std::move(connection));
        if (connections.size() > MaximumConnections)
        {
            // Retain a deterministic endpoint-sorted subset, independently of
            // the order in which Windows happened to enumerate the sockets.
            connections.erase(std::prev(connections.end()));
            truncated = true;
        }
    }

    template<typename Table, typename Row, typename Append>
    DWORD VisitTable(ULONG family, DWORD pid, Append append)
    {
        std::vector<BYTE> storage;
        const DWORD error = ReadTable(family, storage);
        if (error != ERROR_SUCCESS)
            return error;
        constexpr size_t offset = offsetof(Table, table);
        if (storage.size() < offset)
            return ERROR_INVALID_DATA;
        DWORD count{};
        std::memcpy(&count, storage.data(), sizeof(count));
        if (count > (storage.size() - offset) / sizeof(Row))
            return ERROR_INVALID_DATA;
        for (DWORD i = 0; i < count; ++i)
        {
            Row row{};
            std::memcpy(&row, storage.data() + offset + static_cast<size_t>(i) * sizeof(Row), sizeof(row));
            if (row.dwOwningPid != pid || row.dwState == MIB_TCP_STATE_LISTEN)
                continue;
            const DWORD appendError = append(row);
            if (appendError != ERROR_SUCCESS)
                return appendError;
        }
        return ERROR_SUCCESS;
    }
}

ProcessConnections::Snapshot ProcessConnections::Read(DWORD pid)
{
    Snapshot snapshot;
    if (pid == 0)
    {
        snapshot.error = ERROR_INVALID_PARAMETER;
        return snapshot;
    }
    try
    {
        Connections connections;
        const DWORD ipv4Error = VisitTable<MIB_TCPTABLE_OWNER_PID, MIB_TCPROW_OWNER_PID>(AF_INET, pid,
            [&](const MIB_TCPROW_OWNER_PID& row) -> DWORD
            {
                if (row.dwRemoteAddr == 0)
                    return ERROR_SUCCESS;
                Connection connection;
                DWORD error = AddressText(AF_INET, &row.dwLocalAddr, 0, connection.localAddress);
                if (error == ERROR_SUCCESS)
                    error = AddressText(AF_INET, &row.dwRemoteAddr, 0, connection.remoteAddress);
                if (error != ERROR_SUCCESS)
                    return error;
                connection.localPort = ::ntohs(static_cast<unsigned short>(row.dwLocalPort));
                connection.remotePort = ::ntohs(static_cast<unsigned short>(row.dwRemotePort));
                connection.state = StateName(row.dwState);
                AddConnection(connections, std::move(connection), snapshot.truncated);
                return ERROR_SUCCESS;
            });
        const DWORD ipv6Error = VisitTable<MIB_TCP6TABLE_OWNER_PID, MIB_TCP6ROW_OWNER_PID>(AF_INET6, pid,
            [&](const MIB_TCP6ROW_OWNER_PID& row) -> DWORD
            {
                const BYTE zero[16]{};
                if (std::memcmp(row.ucRemoteAddr, zero, sizeof(zero)) == 0)
                    return ERROR_SUCCESS;
                Connection connection;
                connection.ipv6 = true;
                DWORD error = AddressText(AF_INET6, row.ucLocalAddr, row.dwLocalScopeId, connection.localAddress);
                if (error == ERROR_SUCCESS)
                    error = AddressText(AF_INET6, row.ucRemoteAddr, row.dwRemoteScopeId, connection.remoteAddress);
                if (error != ERROR_SUCCESS)
                    return error;
                connection.localPort = ::ntohs(static_cast<unsigned short>(row.dwLocalPort));
                connection.remotePort = ::ntohs(static_cast<unsigned short>(row.dwRemotePort));
                connection.state = StateName(row.dwState);
                AddConnection(connections, std::move(connection), snapshot.truncated);
                return ERROR_SUCCESS;
            });
        snapshot.error = ipv4Error != ERROR_SUCCESS ? ipv4Error : ipv6Error;
        snapshot.connections.assign(connections.begin(), connections.end());
    }
    catch (const std::bad_alloc&)
    {
        snapshot.error = ERROR_NOT_ENOUGH_MEMORY;
    }
    return snapshot;
}

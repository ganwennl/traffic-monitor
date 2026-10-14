#if !defined(TRAFFICMONITOR_COLLECTOR_STANDALONE) && !defined(PROCESS_TRAFFIC_STANDALONE)
#include "stdafx.h"
#endif

#include "ProcessTrafficCollector.h"
#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>
#include <objbase.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cwchar>
#include <limits>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "tdh.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
    const GUID NETWORK_PROVIDER = { 0x7dd42a49, 0x5329, 0x4832, { 0x8d, 0xfd, 0x43, 0xd9, 0x79, 0x15, 0x3a, 0x88 } };
    constexpr std::size_t MAX_PROCESSES = 4096;
    using Clock = std::chrono::steady_clock;

    bool IsUpload(USHORT id)
    {
        return id == 10 || id == 26 || id == 42 || id == 58;
    }

    bool IsTransfer(USHORT id)
    {
        return IsUpload(id) || id == 11 || id == 27 || id == 43 || id == 59;
    }

    bool HasPropertyName(const std::vector<BYTE>& info, ULONG offset, const wchar_t* name)
    {
        const std::size_t bytes = (std::wcslen(name) + 1) * sizeof(wchar_t);
        return offset <= info.size() && bytes <= info.size() - offset
            && std::memcmp(info.data() + offset, name, bytes) == 0;
    }

    // Validate the common prefix through the installed manifest, not a guessed
    // whole-packet struct. TDH on the tested host reports PID/size as UINT32.
    DWORD ValidateSchemas()
    {
        for (USHORT id : { USHORT{10}, USHORT{11}, USHORT{26}, USHORT{27}, USHORT{42}, USHORT{43}, USHORT{58}, USHORT{59} })
        {
            EVENT_RECORD record{};
            record.EventHeader.Size = sizeof(EVENT_HEADER);
            record.EventHeader.Flags = sizeof(void*) == 8 ? EVENT_HEADER_FLAG_64_BIT_HEADER : EVENT_HEADER_FLAG_32_BIT_HEADER;
            record.EventHeader.ProviderId = NETWORK_PROVIDER;
            auto& descriptor = record.EventHeader.EventDescriptor;
            descriptor.Id = id;
            descriptor.Version = 0;
            descriptor.Channel = 16;
            descriptor.Level = TRACE_LEVEL_INFORMATION;
            descriptor.Task = id < 42 ? 10 : 11;
            descriptor.Opcode = id < 42 ? (IsUpload(id) ? 10 : 11) : (IsUpload(id) ? 42 : 43);
            descriptor.Keyword = 0x8000000000000000ULL | ((id == 26 || id == 27 || id == 58 || id == 59) ? 0x20 : 0x10);
            ULONG size{};
            DWORD status = TdhGetEventInformation(&record, 0, nullptr, nullptr, &size);
            if (status != ERROR_INSUFFICIENT_BUFFER)
                return status == ERROR_SUCCESS ? ERROR_INVALID_DATA : status;
            if (size < sizeof(TRACE_EVENT_INFO) + sizeof(EVENT_PROPERTY_INFO))
                return ERROR_INVALID_DATA;
            std::vector<BYTE> storage(size);
            auto* info = reinterpret_cast<TRACE_EVENT_INFO*>(storage.data());
            status = TdhGetEventInformation(&record, 0, nullptr, info, &size);
            if (status != ERROR_SUCCESS)
                return status;
            if (info->TopLevelPropertyCount < 2 || info->PropertyCount < 2)
                return ERROR_INVALID_DATA;
            const auto& pid = info->EventPropertyInfoArray[0];
            const auto& bytes = info->EventPropertyInfoArray[1];
            if (pid.Flags != 0 || bytes.Flags != 0
                || pid.nonStructType.InType != TDH_INTYPE_UINT32 || bytes.nonStructType.InType != TDH_INTYPE_UINT32
                || pid.count != 1 || bytes.count != 1 || pid.length != 4 || bytes.length != 4
                || !HasPropertyName(storage, pid.NameOffset, L"PID") || !HasPropertyName(storage, bytes.NameOffset, L"size"))
                return ERROR_INVALID_DATA;
        }
        return ERROR_SUCCESS;
    }

    struct ProcessHandle
    {
        HANDLE value{};
        bool synchronizable{};
        ProcessHandle() = default;
        ProcessHandle(HANDLE handle, bool sync) : value(handle), synchronizable(sync) {}
        ProcessHandle(const ProcessHandle&) = delete;
        ProcessHandle& operator=(const ProcessHandle&) = delete;
        ProcessHandle(ProcessHandle&& other) noexcept : value(other.value), synchronizable(other.synchronizable)
        {
            other.value = nullptr;
        }
        ProcessHandle& operator=(ProcessHandle&& other) noexcept
        {
            if (this != &other)
            {
                if (value)
                    CloseHandle(value);
                value = other.value;
                synchronizable = other.synchronizable;
                other.value = nullptr;
            }
            return *this;
        }
        ~ProcessHandle()
        {
            if (value)
                CloseHandle(value);
        }
        bool Exited() const
        {
            if (!value)
                return false;
            if (synchronizable)
                return WaitForSingleObject(value, 0) == WAIT_OBJECT_0;
            DWORD code{};
            return GetExitCodeProcess(value, &code) && code != STILL_ACTIVE;
        }
    };
}

class CProcessTrafficCollector::Impl
{
public:
    struct Counter
    {
        DWORD pid{};
        std::wstring name;
        ProcessHandle process;
        ULONGLONG creationTime{};
        std::uint64_t upload{}, download{}, total{};
        Clock::time_point lastSeen{};
    };

    std::mutex lifecycleMutex;
    std::mutex countersMutex;
    std::atomic<bool> stopping{ false }, running{ false }, lost{ true };
    TRACEHANDLE session{};
    TRACEHANDLE consumer{ INVALID_PROCESSTRACE_HANDLE };
    bool ownSession{};
    std::wstring sessionName;
    std::vector<BYTE> properties;
    std::thread worker;
    std::unordered_map<DWORD, Counter> counters;
    std::vector<Counter> retired;
    std::uint64_t intervalUpload{}, intervalDownload{};
    Clock::time_point sampledAt{};

    EVENT_TRACE_PROPERTIES* Properties()
    {
        return reinterpret_cast<EVENT_TRACE_PROPERTIES*>(properties.data());
    }

    void Add(std::uint64_t& value, std::uint64_t bytes)
    {
        const auto limit = (std::numeric_limits<std::uint64_t>::max)();
        if (bytes > limit - value)
        {
            value = limit;
            lost = true;
        }
        else
            value += bytes;
    }

    static Counter NewCounter(DWORD pid, ULONGLONG eventTime)
    {
        Counter value;
        value.pid = pid;
        value.name = L"PID " + std::to_wstring(pid);
        HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
        bool sync = handle != nullptr;
        if (!handle)
            handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        value.process = ProcessHandle(handle, sync);
        if (handle)
        {
            FILETIME created{}, exited{}, kernel{}, user{};
            if (!GetProcessTimes(handle, &created, &exited, &kernel, &user))
                return value; // Without identity time, keep the anonymous PID.
            value.creationTime = (static_cast<ULONGLONG>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
            // A buffered event from a previous owner must not get the new
            // process's image name when Windows has already reused this PID.
            if (eventTime && value.creationTime > eventTime)
            {
                value.process = ProcessHandle();
                value.creationTime = 0;
                return value;
            }
            wchar_t path[1024];
            DWORD size = ARRAYSIZE(path);
            if (QueryFullProcessImageNameW(handle, 0, path, &size))
            {
                std::wstring name(path, size);
                const auto separator = name.find_last_of(L"\\/");
                if (separator != std::wstring::npos)
                    name.erase(0, separator + 1);
                for (auto& ch : name)
                    if (ch < L' ')
                        ch = L' ';
                if (!name.empty())
                    value.name = name.substr(0, 128);
            }
        }
        return value;
    }

    void Receive(const EVENT_RECORD& event)
    {
        if (!IsEqualGUID(event.EventHeader.ProviderId, NETWORK_PROVIDER))
            return;
        const auto id = event.EventHeader.EventDescriptor.Id;
        if (!IsTransfer(id))
            return;
        if (event.EventHeader.EventDescriptor.Version != 0 || !event.UserData || event.UserDataLength < 8)
        {
            lost = true;
            return;
        }
        DWORD pid{}, bytes{};
        std::memcpy(&pid, event.UserData, sizeof(pid));
        std::memcpy(&bytes, static_cast<const BYTE*>(event.UserData) + sizeof(pid), sizeof(bytes));
        if (!bytes)
            return;
        const auto now = Clock::now();
        std::lock_guard<std::mutex> lock(countersMutex);
        const bool upload = IsUpload(id);
        Add(upload ? intervalUpload : intervalDownload, bytes);
        auto found = counters.find(pid);
        if (found != counters.end() && found->second.process.Exited())
        {
            if (retired.size() < MAX_PROCESSES)
                retired.push_back(std::move(found->second));
            else
                lost = true;
            counters.erase(found);
            found = counters.end();
        }
        if (found == counters.end())
        {
            if (counters.size() >= MAX_PROCESSES)
            {
                lost = true;
                return;
            }
            found = counters.emplace(pid, NewCounter(pid, event.EventHeader.TimeStamp.QuadPart)).first;
        }
        auto& counter = found->second;
        if (counter.creationTime && event.EventHeader.TimeStamp.QuadPart > 0
            && static_cast<ULONGLONG>(event.EventHeader.TimeStamp.QuadPart) < counter.creationTime)
        {
            // Preserve aggregate bytes, but do not assign old buffered bytes
            // to the new PID owner. The snapshot explicitly becomes partial.
            lost = true;
            return;
        }
        Add(upload ? counter.upload : counter.download, bytes);
        Add(counter.total, bytes);
        counter.lastSeen = now;
    }

    static void WINAPI EventCallback(EVENT_RECORD* event)
    {
        auto* self = static_cast<Impl*>(event->UserContext);
        if (!self || self->stopping)
            return;
        try
        {
            self->Receive(*event);
        }
        catch (...)
        {
            // Exceptions must never escape through an ETW callback.
            self->lost = true;
        }
    }

    static ULONG WINAPI BufferCallback(EVENT_TRACE_LOGFILEW* buffer)
    {
        auto* self = static_cast<Impl*>(buffer->Context);
        if (self && buffer->EventsLost)
            self->lost = true;
        return self && !self->stopping ? TRUE : FALSE;
    }

    void StopLocked()
    {
        stopping = true;
        if (ownSession)
        {
            EnableTraceEx2(session, &NETWORK_PROVIDER, EVENT_CONTROL_CODE_DISABLE_PROVIDER, TRACE_LEVEL_INFORMATION, 0, 0, 1000, nullptr);
            // Only use the handle of a session we successfully created. Never
            // attach to, reconfigure or stop an existing logger by its name.
            for (int attempt = 0; attempt < 3; ++attempt)
            {
                const DWORD result = ControlTraceW(session, sessionName.c_str(), Properties(), EVENT_TRACE_CONTROL_STOP);
                if (result == ERROR_SUCCESS || result == ERROR_WMI_INSTANCE_NOT_FOUND || result == ERROR_INVALID_HANDLE)
                {
                    ownSession = false;
                    break;
                }
                lost = true;
                if (attempt < 2)
                    Sleep(30);
            }
        }
        if (consumer != INVALID_PROCESSTRACE_HANDLE)
            CloseTrace(consumer);
        if (worker.joinable())
            worker.join();
        consumer = INVALID_PROCESSTRACE_HANDLE;
        running = false;
        lost = true;
        if (!ownSession)
        {
            session = 0;
            properties.clear();
            sessionName.clear();
        }
        std::lock_guard<std::mutex> lock(countersMutex);
        counters.clear();
        retired.clear();
        intervalUpload = intervalDownload = 0;
    }

    DWORD Start()
    {
        std::lock_guard<std::mutex> lock(lifecycleMutex);
        if (running)
            return ERROR_SUCCESS;
        StopLocked();
        if (ownSession)
            return ERROR_BUSY; // Keep the failed session identity for cleanup retries.
        try
        {
            DWORD status = ValidateSchemas();
            if (status != ERROR_SUCCESS)
                return status;
            GUID instance{};
            if (FAILED(CoCreateGuid(&instance)))
                return ERROR_GEN_FAILURE;
            wchar_t guid[40]{};
            if (!StringFromGUID2(instance, guid, ARRAYSIZE(guid)))
                return ERROR_GEN_FAILURE;
            sessionName = L"TrafficMonitor.ProcessTraffic." + std::to_wstring(GetCurrentProcessId()) + L"." + guid;
            properties.resize(sizeof(EVENT_TRACE_PROPERTIES) + (sessionName.size() + 1) * sizeof(wchar_t));
            std::fill(properties.begin(), properties.end(), BYTE{});
            auto* props = Properties();
            props->Wnode.BufferSize = static_cast<ULONG>(properties.size());
            props->Wnode.Guid = instance;
            props->Wnode.ClientContext = 1;
            props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
            props->BufferSize = 64;
            props->MinimumBuffers = 4;
            props->MaximumBuffers = 32;
            props->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
            props->FlushTimer = 1;
            props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
            status = StartTraceW(&session, sessionName.c_str(), props);
            if (status != ERROR_SUCCESS)
            {
                session = 0;
                return status;
            }
            ownSession = true;
            status = EnableTraceEx2(session, &NETWORK_PROVIDER, EVENT_CONTROL_CODE_ENABLE_PROVIDER, TRACE_LEVEL_INFORMATION, 0x30, 0, 1000, nullptr);
            if (status != ERROR_SUCCESS)
            {
                StopLocked();
                return status;
            }
            EVENT_TRACE_LOGFILEW trace{};
            trace.LoggerName = &sessionName[0];
            trace.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
            trace.EventRecordCallback = EventCallback;
            trace.BufferCallback = BufferCallback;
            trace.Context = this;
            consumer = OpenTraceW(&trace);
            if (consumer == INVALID_PROCESSTRACE_HANDLE)
            {
                status = GetLastError();
                StopLocked();
                return status;
            }
            stopping = false;
            lost = false;
            sampledAt = Clock::now();
            running = true;
            const TRACEHANDLE handle = consumer;
            worker = std::thread([this, handle]() {
                TRACEHANDLE localHandle = handle;
                const DWORD result = ProcessTrace(&localHandle, 1, nullptr, nullptr);
                if (!stopping || (result != ERROR_SUCCESS && result != ERROR_CANCELLED))
                    lost = true;
                running = false;
            });
            return ERROR_SUCCESS;
        }
        catch (...)
        {
            StopLocked();
            return ERROR_NOT_ENOUGH_MEMORY;
        }
    }

    static std::uint64_t Rate(std::uint64_t bytes, long double seconds)
    {
        if (seconds <= 0)
            return 0;
        const long double value = static_cast<long double>(bytes) / seconds;
        const auto limit = (std::numeric_limits<std::uint64_t>::max)();
        return value >= static_cast<long double>(limit) ? limit : static_cast<std::uint64_t>(value);
    }

    Snapshot TakeSnapshot()
    {
        std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex);
        Snapshot snapshot;
        if (!ownSession || !running)
        {
            snapshot.dataLost = true;
            return snapshot;
        }
        snapshot.available = true;
        const DWORD query = ControlTraceW(session, sessionName.c_str(), Properties(), EVENT_TRACE_CONTROL_QUERY);
        if (query != ERROR_SUCCESS || Properties()->EventsLost || Properties()->RealTimeBuffersLost)
            lost = true;
        const auto now = Clock::now();
        std::lock_guard<std::mutex> countersLock(countersMutex);
        const long double seconds = std::chrono::duration<long double>(now - sampledAt).count();
        if (seconds <= 0)
            lost = true;
        snapshot.uploadBytesPerSecond = Rate(intervalUpload, seconds);
        snapshot.downloadBytesPerSecond = Rate(intervalDownload, seconds);
        intervalUpload = intervalDownload = 0;
        const auto addRow = [&](Counter& counter) {
            if (counter.total)
                snapshot.rows.push_back({ counter.name, counter.pid, Rate(counter.upload, seconds), Rate(counter.download, seconds), counter.total });
            counter.upload = counter.download = 0;
        };
        for (auto& counter : retired)
            addRow(counter);
        retired.clear();
        for (auto iterator = counters.begin(); iterator != counters.end();)
        {
            auto& counter = iterator->second;
            addRow(counter);
            // Process handles bind identity despite PID reuse. Unknown owners
            // are short lived to avoid accumulating stale process identities.
            if (counter.process.Exited() || (!counter.process.value && now - counter.lastSeen > std::chrono::seconds(30)))
                iterator = counters.erase(iterator);
            else
                ++iterator;
        }
        sampledAt = now;
        snapshot.dataLost = lost;
        std::sort(snapshot.rows.begin(), snapshot.rows.end(), [](const Row& left, const Row& right) {
            const long double a = static_cast<long double>(left.uploadBytesPerSecond) + left.downloadBytesPerSecond;
            const long double b = static_cast<long double>(right.uploadBytesPerSecond) + right.downloadBytesPerSecond;
            if (a != b)
                return a > b;
            return left.totalBytes == right.totalBytes ? left.pid < right.pid : left.totalBytes > right.totalBytes;
        });
        return snapshot;
    }
};

CProcessTrafficCollector::CProcessTrafficCollector() : m_impl(std::make_unique<Impl>()) {}
CProcessTrafficCollector::~CProcessTrafficCollector() { Stop(); }
DWORD CProcessTrafficCollector::Start() { return m_impl->Start(); }
void CProcessTrafficCollector::Stop()
{
    std::lock_guard<std::mutex> lock(m_impl->lifecycleMutex);
    m_impl->StopLocked();
}
CProcessTrafficCollector::Snapshot CProcessTrafficCollector::TakeSnapshot() { return m_impl->TakeSnapshot(); }

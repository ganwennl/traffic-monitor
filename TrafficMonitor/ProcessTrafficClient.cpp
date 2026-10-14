#ifndef PROCESS_TRAFFIC_STANDALONE
#include "stdafx.h"
#else
#include <windows.h>
#include <algorithm>
#include <iterator>
#include <vector>
#endif
#include "ProcessTrafficClient.h"
#include <shellapi.h>
#include <sddl.h>
#include <process.h>
#include <cstdint>
#include <cstring>
#include <limits>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")

namespace
{
constexpr uint32_t kMagic = 0x54504D54;
constexpr uint32_t kProtocolVersion = 1;
constexpr size_t kMaximumRows = 64;
constexpr DWORD kIoTimeout = 5000;
constexpr DWORD kConnectionTimeout = 30000;
constexpr wchar_t kCollectorArgument[] = L"--process-traffic-collector";

enum class MessageKind : uint32_t { Hello = 1, Status = 2, Snapshot = 3 };

struct WireRow
{
    uint32_t pid{};
    uint32_t reserved{};
    uint64_t uploadBytesPerSecond{};
    uint64_t downloadBytesPerSecond{};
    uint64_t totalBytes{};
    wchar_t name[260]{};
};

struct WireMessage
{
    uint32_t magic{ kMagic };
    uint32_t version{ kProtocolVersion };
    uint32_t kind{};
    uint32_t error{};
    uint32_t rowCount{};
    uint32_t dataLost{};
    uint64_t uploadBytesPerSecond{};
    uint64_t downloadBytesPerSecond{};
    WireRow rows[kMaximumRows]{};
};

static_assert(sizeof(wchar_t) == 2, "IPC uses Windows UTF-16");
static_assert(sizeof(WireRow) == 552, "Keep the IPC row layout fixed");
static_assert(sizeof(WireMessage) == 35368, "Keep the IPC message layout fixed");

class Handle
{
public:
    explicit Handle(HANDLE value = nullptr) : m_value(value) {}
    ~Handle() { Reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE Get() const { return m_value; }
    explicit operator bool() const { return m_value && m_value != INVALID_HANDLE_VALUE; }
    void Reset(HANDLE value = nullptr)
    {
        if (*this)
            CloseHandle(m_value);
        m_value = value;
    }
private:
    HANDLE m_value;
};

bool IsStopped(HANDLE stop)
{
    return stop && WaitForSingleObject(stop, 0) == WAIT_OBJECT_0;
}

std::wstring ErrorText(DWORD error)
{
    if (error == ERROR_CANCELLED || error == ERROR_OPERATION_ABORTED)
        return L"未获授权，可点击启用后重新授权";
    if (error == ERROR_ACCESS_DENIED)
        return L"需要同一 Windows 用户的管理员授权";
    if (error == ERROR_TIMEOUT)
        return L"采集连接超时，可重新启用";
    if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED || error == ERROR_PROCESS_ABORTED)
        return L"进程流量采集已断开，可重新启用";
    return L"暂时无法采集程序流量，可点击重新启用";
}

void LimitAndSortRows(std::vector<CProcessTrafficCollector::Row>& rows)
{
    const auto rate = [](const CProcessTrafficCollector::Row& row)
    {
        const uint64_t maximum = (std::numeric_limits<uint64_t>::max)();
        return row.uploadBytesPerSecond > maximum - row.downloadBytesPerSecond
            ? maximum : row.uploadBytesPerSecond + row.downloadBytesPerSecond;
    };
    std::sort(rows.begin(), rows.end(), [&](const auto& left, const auto& right)
    {
        if (rate(left) != rate(right))
            return rate(left) > rate(right);
        if (left.totalBytes != right.totalBytes)
            return left.totalBytes > right.totalBytes;
        return left.pid < right.pid;
    });
    if (rows.size() > kMaximumRows)
        rows.resize(kMaximumRows);
}

std::wstring ExecutablePath(HANDLE process)
{
    std::vector<wchar_t> buffer(32768);
    DWORD length = static_cast<DWORD>(buffer.size());
    if (!QueryFullProcessImageNameW(process, 0, buffer.data(), &length) || length == 0)
        return {};
    return std::wstring(buffer.data(), length);
}

bool ReadUserSid(HANDLE process, std::vector<unsigned char>& sid)
{
    HANDLE tokenValue{};
    if (!OpenProcessToken(process, TOKEN_QUERY, &tokenValue))
        return false;
    Handle token(tokenValue);
    DWORD length{};
    GetTokenInformation(token.Get(), TokenUser, nullptr, 0, &length);
    if (length == 0)
        return false;
    std::vector<unsigned char> tokenBuffer(length);
    if (!GetTokenInformation(token.Get(), TokenUser, tokenBuffer.data(), length, &length))
        return false;
    const auto user = reinterpret_cast<TOKEN_USER*>(tokenBuffer.data());
    if (!IsValidSid(user->User.Sid))
        return false;
    sid.resize(GetLengthSid(user->User.Sid));
    return CopySid(static_cast<DWORD>(sid.size()), sid.data(), user->User.Sid) != FALSE;
}

DWORD CreatePrivatePipe(const std::wstring& name, Handle& pipe)
{
    std::vector<unsigned char> sid;
    if (!ReadUserSid(GetCurrentProcess(), sid))
    {
        const DWORD error = GetLastError();
        return error != ERROR_SUCCESS ? error : ERROR_INVALID_SID;
    }
    LPWSTR sidString{};
    if (!ConvertSidToStringSidW(sid.data(), &sidString))
        return GetLastError();
    const std::wstring descriptor = L"D:P(A;;GA;;;SY)(A;;GA;;;" + std::wstring(sidString) + L")";
    LocalFree(sidString);
    PSECURITY_DESCRIPTOR securityDescriptor{};
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(descriptor.c_str(), SDDL_REVISION_1,
        &securityDescriptor, nullptr))
        return GetLastError();
    SECURITY_ATTRIBUTES attributes{ sizeof(SECURITY_ATTRIBUTES), securityDescriptor, FALSE };
    HANDLE value = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
        sizeof(WireMessage), sizeof(WireMessage), 0, &attributes);
    const DWORD error = value == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    LocalFree(securityDescriptor);
    pipe.Reset(value);
    return error;
}

std::wstring PipeName(DWORD parentPid, const std::wstring& nonce)
{
    return L"\\\\.\\pipe\\TrafficMonitor.ProcessTraffic." + std::to_wstring(parentPid) + L"." + nonce;
}

std::wstring CreateNonce()
{
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid)))
        return {};
    constexpr wchar_t digits[] = L"0123456789abcdef";
    const auto bytes = reinterpret_cast<const unsigned char*>(&guid);
    std::wstring value(32, L'0');
    for (size_t i = 0; i < sizeof(guid); ++i)
    {
        value[i * 2] = digits[bytes[i] >> 4];
        value[i * 2 + 1] = digits[bytes[i] & 15];
    }
    return value;
}

// This drain is required before releasing the stack OVERLAPPED/event. All such
// operations belong to the shared-lifetime worker; the UI's Stop is bounded.
void CancelAndDrain(HANDLE pipe, OVERLAPPED& operation)
{
    CancelIoEx(pipe, &operation);
    DWORD transferred{};
    GetOverlappedResult(pipe, &operation, &transferred, TRUE);
}

DWORD AwaitIo(HANDLE pipe, OVERLAPPED& operation, HANDLE stop, HANDLE peer,
    DWORD timeout, DWORD& transferred)
{
    HANDLE waits[3]{ operation.hEvent, stop, peer };
    DWORD count = 1;
    if (stop)
        waits[count++] = stop;
    if (peer)
        waits[count++] = peer;
    const DWORD result = WaitForMultipleObjects(count, waits, FALSE, timeout);
    if (result == WAIT_OBJECT_0)
        return GetOverlappedResult(pipe, &operation, &transferred, FALSE) ? ERROR_SUCCESS : GetLastError();
    const DWORD error = result == WAIT_TIMEOUT ? ERROR_TIMEOUT
        : result == WAIT_FAILED ? GetLastError()
        : stop && result == WAIT_OBJECT_0 + 1 ? ERROR_OPERATION_ABORTED : ERROR_PROCESS_ABORTED;
    CancelAndDrain(pipe, operation);
    return error;
}

DWORD ConnectServer(HANDLE pipe, HANDLE stop, HANDLE helper)
{
    Handle completed(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!completed)
        return GetLastError();
    OVERLAPPED operation{};
    operation.hEvent = completed.Get();
    if (ConnectNamedPipe(pipe, &operation))
        return ERROR_SUCCESS;
    DWORD error = GetLastError();
    if (error == ERROR_PIPE_CONNECTED)
        return ERROR_SUCCESS;
    if (error != ERROR_IO_PENDING)
        return error;
    DWORD transferred{};
    return AwaitIo(pipe, operation, stop, helper, kConnectionTimeout, transferred);
}

DWORD Transfer(HANDLE pipe, WireMessage& message, bool write, HANDLE stop, HANDLE peer, DWORD timeout = kIoTimeout)
{
    if (IsStopped(stop))
        return ERROR_OPERATION_ABORTED;
    Handle completed(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!completed)
        return GetLastError();
    OVERLAPPED operation{};
    operation.hEvent = completed.Get();
    DWORD transferred{};
    const BOOL success = write
        ? WriteFile(pipe, &message, sizeof(message), &transferred, &operation)
        : ReadFile(pipe, &message, sizeof(message), &transferred, &operation);
    if (!success)
    {
        DWORD error = GetLastError();
        if (error != ERROR_IO_PENDING)
            return error;
        error = AwaitIo(pipe, operation, stop, peer, timeout, transferred);
        if (error != ERROR_SUCCESS)
            return error;
    }
    return transferred == sizeof(message) ? ERROR_SUCCESS : ERROR_INVALID_DATA;
}

bool IsValidMessage(const WireMessage& message)
{
    if (message.magic != kMagic || message.version != kProtocolVersion ||
        message.rowCount > kMaximumRows || message.dataLost > 1)
        return false;
    for (uint32_t i = 0; i < message.rowCount; ++i)
    {
        if (message.rows[i].reserved != 0 ||
            std::find(std::begin(message.rows[i].name), std::end(message.rows[i].name), L'\0') == std::end(message.rows[i].name))
            return false;
    }
    return true;
}

bool ParsePid(const wchar_t* text, DWORD& pid)
{
    if (!text || !*text)
        return false;
    uint64_t value{};
    for (const wchar_t* position = text; *position; ++position)
    {
        if (*position < L'0' || *position > L'9')
            return false;
        value = value * 10 + (*position - L'0');
        if (value > (std::numeric_limits<DWORD>::max)())
            return false;
    }
    pid = static_cast<DWORD>(value);
    return pid != 0 && pid != GetCurrentProcessId();
}

bool IsValidNonce(const wchar_t* text)
{
    if (!text || wcslen(text) != 32)
        return false;
    for (size_t i = 0; i < 32; ++i)
    {
        if (!((text[i] >= L'0' && text[i] <= L'9') || (text[i] >= L'a' && text[i] <= L'f') ||
            (text[i] >= L'A' && text[i] <= L'F')))
            return false;
    }
    return true;
}

DWORD RunHelper(DWORD parentPid, const std::wstring& nonce)
{
    Handle parent(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, parentPid));
    if (!parent)
        return GetLastError();
    if (WaitForSingleObject(parent.Get(), 0) != WAIT_TIMEOUT)
        return ERROR_PROCESS_ABORTED;
    const std::wstring ownPath = ExecutablePath(GetCurrentProcess());
    const std::wstring parentPath = ExecutablePath(parent.Get());
    std::vector<unsigned char> ownSid, parentSid;
    if (ownPath.empty() || parentPath.empty() || _wcsicmp(ownPath.c_str(), parentPath.c_str()) != 0 ||
        !ReadUserSid(GetCurrentProcess(), ownSid) || !ReadUserSid(parent.Get(), parentSid) ||
        !EqualSid(ownSid.data(), parentSid.data()))
        return ERROR_ACCESS_DENIED;

    const std::wstring name = PipeName(parentPid, nonce);
    Handle pipe;
    const ULONGLONG deadline = GetTickCount64() + kConnectionTimeout;
    while (!pipe)
    {
        pipe.Reset(CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr));
        if (pipe)
            break;
        const DWORD error = GetLastError();
        if (error != ERROR_PIPE_BUSY && error != ERROR_FILE_NOT_FOUND)
            return error;
        if (GetTickCount64() >= deadline)
            return ERROR_TIMEOUT;
        if (WaitForSingleObject(parent.Get(), 100) != WAIT_TIMEOUT)
            return ERROR_PROCESS_ABORTED;
    }
    ULONG serverPid{};
    if (!GetNamedPipeServerProcessId(pipe.Get(), &serverPid) || serverPid != parentPid)
        return ERROR_ACCESS_DENIED;
    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(pipe.Get(), &mode, nullptr, nullptr))
        return GetLastError();

    // Collection starts only after the parent has verified the helper PID.
    WireMessage hello{};
    DWORD error = Transfer(pipe.Get(), hello, false, nullptr, parent.Get());
    WireMessage expectedHello{};
    expectedHello.kind = static_cast<uint32_t>(MessageKind::Hello);
    if (error != ERROR_SUCCESS)
        return error;
    if (std::memcmp(&hello, &expectedHello, sizeof(hello)) != 0)
        return ERROR_INVALID_DATA;

    CProcessTrafficCollector collector;
    error = collector.Start();
    WireMessage status{};
    status.kind = static_cast<uint32_t>(MessageKind::Status);
    status.error = error;
    const DWORD writeError = Transfer(pipe.Get(), status, true, nullptr, parent.Get());
    if (writeError != ERROR_SUCCESS || error != ERROR_SUCCESS)
    {
        collector.Stop();
        return writeError != ERROR_SUCCESS ? writeError : error;
    }

    while (WaitForSingleObject(parent.Get(), 1000) == WAIT_TIMEOUT)
    {
        const auto snapshot = collector.TakeSnapshot();
        if (!snapshot.available)
        {
            WireMessage unavailable{};
            unavailable.kind = static_cast<uint32_t>(MessageKind::Status);
            unavailable.error = ERROR_PROCESS_ABORTED;
            error = Transfer(pipe.Get(), unavailable, true, nullptr, parent.Get());
            break;
        }
        auto rows = snapshot.rows;
        LimitAndSortRows(rows);
        WireMessage message{};
        message.kind = static_cast<uint32_t>(MessageKind::Snapshot);
        message.rowCount = static_cast<uint32_t>(rows.size());
        message.dataLost = snapshot.dataLost ? 1 : 0;
        message.uploadBytesPerSecond = snapshot.uploadBytesPerSecond;
        message.downloadBytesPerSecond = snapshot.downloadBytesPerSecond;
        for (uint32_t i = 0; i < message.rowCount; ++i)
        {
            const auto& row = rows[i];
            auto& target = message.rows[i];
            target.pid = row.pid;
            target.uploadBytesPerSecond = row.uploadBytesPerSecond;
            target.downloadBytesPerSecond = row.downloadBytesPerSecond;
            target.totalBytes = row.totalBytes;
            const size_t characters = (std::min)(row.name.size(), std::size(target.name) - 1);
            std::copy_n(row.name.data(), characters, target.name);
        }
        error = Transfer(pipe.Get(), message, true, nullptr, parent.Get());
        if (error != ERROR_SUCCESS)
            break;
    }
    collector.Stop();
    return error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA || error == ERROR_PIPE_NOT_CONNECTED ||
        error == ERROR_PROCESS_ABORTED ? ERROR_SUCCESS : error;
}
}

struct CProcessTrafficClient::State
{
    Handle stop{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
    Handle done{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
    HWND owner{};
    bool allowElevation{};
    mutable std::mutex snapshotMutex;
    Snapshot snapshot;

    void Publish(const Snapshot& value)
    {
        std::lock_guard<std::mutex> lock(snapshotMutex);
        if (!IsStopped(stop.Get()))
            snapshot = value;
    }

    void Status(const std::wstring& text, bool canEnable)
    {
        Snapshot value;
        value.status = text;
        value.canEnable = canEnable;
        Publish(value);
    }
};

CProcessTrafficClient::~CProcessTrafficClient()
{
    Stop();
}

void CProcessTrafficClient::Start(HWND owner, bool allowElevation)
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    // A live attempt includes a pending UAC prompt. Repeated refreshes never
    // create another prompt or another elevated collector.
    if (m_state && m_state->done && WaitForSingleObject(m_state->done.Get(), 0) != WAIT_OBJECT_0)
        return;
    auto state = std::make_shared<State>();
    state->owner = owner;
    state->allowElevation = allowElevation;
    state->snapshot.status = L"正在启用进程流量采集…";
    state->snapshot.canEnable = false;
    m_state = state;
    if (!state->stop || !state->done)
    {
        state->snapshot.status = L"无法初始化进程流量采集";
        state->snapshot.canEnable = true;
        if (state->done)
            SetEvent(state->done.Get());
        return;
    }
    auto context = new std::shared_ptr<State>(state);
    const uintptr_t thread = _beginthreadex(nullptr, 0, WorkerThread, context, 0, nullptr);
    if (!thread)
    {
        delete context;
        state->snapshot.status = L"无法创建进程流量采集线程";
        state->snapshot.canEnable = true;
        SetEvent(state->done.Get());
        return;
    }
    CloseHandle(reinterpret_cast<HANDLE>(thread));
}

void CProcessTrafficClient::Stop()
{
    std::shared_ptr<State> state;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        state = m_state;
    }
    if (!state)
        return;
    const bool alreadyStopped = IsStopped(state->stop.Get());
    if (state->stop)
        SetEvent(state->stop.Get());
    {
        std::lock_guard<std::mutex> lock(state->snapshotMutex);
        state->snapshot = Snapshot{};
        state->snapshot.status = L"进程流量采集已停止";
    }
    // ShellExecuteEx can remain inside an unanswered Windows UAC prompt.
    // The worker retains State and checks stop before granting its handshake.
    if (state->done)
        WaitForSingleObject(state->done.Get(), alreadyStopped ? 0 : 3000);
}

CProcessTrafficClient::Snapshot CProcessTrafficClient::TakeSnapshot() const
{
    std::shared_ptr<State> state;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        state = m_state;
    }
    if (!state)
        return {};
    std::lock_guard<std::mutex> lock(state->snapshotMutex);
    return state->snapshot;
}

unsigned __stdcall CProcessTrafficClient::WorkerThread(void* context)
{
    std::unique_ptr<std::shared_ptr<State>> holder(static_cast<std::shared_ptr<State>*>(context));
    auto state = *holder;
    try
    {
        Worker(state);
    }
    catch (...)
    {
        state->Status(L"进程流量采集失败，可重新启用", true);
    }
    SetEvent(state->done.Get());
    return 0;
}

void CProcessTrafficClient::Worker(const std::shared_ptr<State>& state)
{
    if (IsStopped(state->stop.Get()))
        return;
    CProcessTrafficCollector localCollector;
    DWORD error = localCollector.Start();
    if (error == ERROR_SUCCESS)
    {
        while (!IsStopped(state->stop.Get()))
        {
            const auto source = localCollector.TakeSnapshot();
            if (!source.available)
            {
                state->Status(ErrorText(ERROR_PROCESS_ABORTED), true);
                break;
            }
            Snapshot snapshot;
            snapshot.rows = source.rows;
            LimitAndSortRows(snapshot.rows);
            snapshot.uploadBytesPerSecond = source.uploadBytesPerSecond;
            snapshot.downloadBytesPerSecond = source.downloadBytesPerSecond;
            snapshot.dataLost = source.dataLost;
            snapshot.status = source.dataLost ? L"正在统计；部分采集事件丢失" : L"正在统计进程流量";
            snapshot.canEnable = false;
            state->Publish(snapshot);
            if (WaitForSingleObject(state->stop.Get(), 1000) != WAIT_TIMEOUT)
                break;
        }
        localCollector.Stop();
        return;
    }
    localCollector.Stop();
    if (IsStopped(state->stop.Get()))
        return;
    if (error != ERROR_ACCESS_DENIED || !state->allowElevation)
    {
        state->Status(error == ERROR_ACCESS_DENIED ? L"需要管理员授权，点击启用后授权" : ErrorText(error), true);
        return;
    }

    const std::wstring nonce = CreateNonce();
    const std::wstring path = ExecutablePath(GetCurrentProcess());
    if (nonce.empty() || path.empty())
    {
        state->Status(L"无法创建安全采集连接", true);
        return;
    }
    Handle pipe;
    error = CreatePrivatePipe(PipeName(GetCurrentProcessId(), nonce), pipe);
    if (error != ERROR_SUCCESS)
    {
        state->Status(ErrorText(error), true);
        return;
    }
    state->Status(L"等待 Windows 管理员授权…", false);
    const std::wstring arguments = std::wstring(kCollectorArgument) + L" " + std::to_wstring(GetCurrentProcessId()) + L" " + nonce;
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SHELLEXECUTEINFOW launch{ sizeof(SHELLEXECUTEINFOW) };
    launch.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    launch.hwnd = IsWindow(state->owner) ? state->owner : nullptr;
    launch.lpVerb = L"runas";
    launch.lpFile = path.c_str();
    launch.lpParameters = arguments.c_str();
    launch.nShow = SW_HIDE;
    if (IsStopped(state->stop.Get()))
    {
        if (SUCCEEDED(comResult))
            CoUninitialize();
        return;
    }
    const BOOL launched = ShellExecuteExW(&launch);
    error = launched ? ERROR_SUCCESS : GetLastError();
    if (SUCCEEDED(comResult))
        CoUninitialize();
    Handle helper(launch.hProcess);
    if (IsStopped(state->stop.Get()))
        return;
    if (!launched || !helper)
    {
        state->Status(ErrorText(error == ERROR_SUCCESS ? ERROR_PROCESS_ABORTED : error), true);
        return;
    }
    state->Status(L"正在连接进程流量采集…", false);
    error = ConnectServer(pipe.Get(), state->stop.Get(), helper.Get());
    ULONG clientPid{};
    if (error == ERROR_SUCCESS && (!GetNamedPipeClientProcessId(pipe.Get(), &clientPid) ||
        clientPid != GetProcessId(helper.Get())))
        error = ERROR_ACCESS_DENIED;
    if (error != ERROR_SUCCESS)
    {
        DWORD helperExit{};
        if (error == ERROR_PROCESS_ABORTED && GetExitCodeProcess(helper.Get(), &helperExit) && helperExit == ERROR_ACCESS_DENIED)
            error = ERROR_ACCESS_DENIED;
        state->Status(ErrorText(error), true);
        return;
    }
    WireMessage hello{};
    hello.kind = static_cast<uint32_t>(MessageKind::Hello);
    error = Transfer(pipe.Get(), hello, true, state->stop.Get(), helper.Get());
    WireMessage message{};
    if (error == ERROR_SUCCESS)
        error = Transfer(pipe.Get(), message, false, state->stop.Get(), helper.Get());
    if (error == ERROR_SUCCESS && (!IsValidMessage(message) || message.kind != static_cast<uint32_t>(MessageKind::Status) ||
        message.rowCount != 0 || message.dataLost != 0))
        error = ERROR_INVALID_DATA;
    if (error == ERROR_SUCCESS)
        error = message.error;
    if (error != ERROR_SUCCESS)
    {
        state->Status(ErrorText(error), true);
        return;
    }

    while (!IsStopped(state->stop.Get()))
    {
        message = WireMessage{};
        error = Transfer(pipe.Get(), message, false, state->stop.Get(), helper.Get());
        if (error != ERROR_SUCCESS)
            break;
        if (IsValidMessage(message) && message.kind == static_cast<uint32_t>(MessageKind::Status) &&
            message.rowCount == 0 && message.error != ERROR_SUCCESS)
        {
            error = message.error;
            break;
        }
        if (!IsValidMessage(message) || message.kind != static_cast<uint32_t>(MessageKind::Snapshot) || message.error != 0)
        {
            error = ERROR_INVALID_DATA;
            break;
        }
        Snapshot snapshot;
        snapshot.uploadBytesPerSecond = message.uploadBytesPerSecond;
        snapshot.downloadBytesPerSecond = message.downloadBytesPerSecond;
        snapshot.dataLost = message.dataLost != 0;
        snapshot.status = snapshot.dataLost ? L"正在统计；部分采集事件丢失" : L"正在统计进程流量";
        snapshot.canEnable = false;
        snapshot.rows.reserve(message.rowCount);
        for (uint32_t i = 0; i < message.rowCount; ++i)
        {
            const auto& wire = message.rows[i];
            Row row;
            row.name = wire.name;
            row.pid = wire.pid;
            row.uploadBytesPerSecond = wire.uploadBytesPerSecond;
            row.downloadBytesPerSecond = wire.downloadBytesPerSecond;
            row.totalBytes = wire.totalBytes;
            snapshot.rows.push_back(std::move(row));
        }
        state->Publish(snapshot);
    }
    state->Status(ErrorText(error), true);
}

bool CProcessTrafficClient::IsCollectorCommandLine()
{
    int count{};
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments)
        return false;
    const bool helper = count >= 2 && wcscmp(arguments[1], kCollectorArgument) == 0;
    LocalFree(arguments);
    return helper;
}

bool CProcessTrafficClient::RunCollectorCommandLine(int& exitCode)
{
    int count{};
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments)
        return false;
    if (count < 2 || wcscmp(arguments[1], kCollectorArgument) != 0)
    {
        LocalFree(arguments);
        return false;
    }
    DWORD parentPid{};
    const bool valid = count == 4 && ParsePid(arguments[2], parentPid) && IsValidNonce(arguments[3]);
    const std::wstring nonce = valid ? arguments[3] : L"";
    LocalFree(arguments);
    exitCode = ERROR_INVALID_PARAMETER;
    if (valid)
    {
        try
        {
            exitCode = static_cast<int>(RunHelper(parentPid, nonce));
        }
        catch (...)
        {
            exitCode = ERROR_UNHANDLED_EXCEPTION;
        }
    }
    return true;
}

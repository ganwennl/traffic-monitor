#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

// Independent of MFC: the worker never owns a window or application pointer.
namespace PersonalUpdate
{
constexpr size_t MaxResponseBytes = 512 * 1024;
enum class Status { Error, Current, Available, Cancelled };
struct Result
{
    Status status{ Status::Error };
    bool manual{};
    std::string version;
    std::string releaseUrl;
};
struct Response
{
    unsigned status{};
    std::string body;
};
using Cancellation = std::shared_ptr<std::atomic_bool>;
using Fetcher = std::function<Response(const Cancellation&)>;

bool ParseVersion(const std::string& text, std::array<unsigned, 3>& numbers);
Result ParseRelease(const Response& response, const std::string& currentVersion);

class Checker
{
public:
    Checker() = default;
    ~Checker();
    Checker(const Checker&) = delete;
    Checker& operator=(const Checker&) = delete;
    bool Start(const std::string& currentVersion, bool manual, Fetcher fetcher = {});
    bool Running() const;
    bool TakeResult(Result& result);
    void Stop();
private:
    struct State;
    std::shared_ptr<State> m_state;
    std::thread m_worker;
};
}

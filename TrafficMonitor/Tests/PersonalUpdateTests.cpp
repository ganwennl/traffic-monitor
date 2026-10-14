#include "../PersonalUpdate.h"
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>

using namespace PersonalUpdate;
unsigned checks{};
void Check(bool value, const char* name) { ++checks; if (!value) throw std::runtime_error(name); }
std::string Release(const std::string& version = "1.86.5-personal")
{
    const std::string tag = "v" + version;
    const std::string name = "TrafficMonitor-" + version + "-Windows-x64-Lite-portable.zip";
    return "{\"body\":\"Unicode \\u4e2d\\u6587 and escaped \\\"tag_name\\\":\\\"v99.0.0\\\"\",\"tag_name\":\"" + tag +
        "\",\"draft\":false,\"prerelease\":false,\"published_at\":\"2026-10-15T00:00:00Z\",\"html_url\":\"https://github.com/ganwennl/traffic-monitor/releases/tag/" + tag +
        "\",\"assets\":[{\"name\":\"" + name + "\",\"size\":1234,\"state\":\"uploaded\",\"browser_download_url\":\"https://github.com/ganwennl/traffic-monitor/releases/download/" + tag + "/" + name + "\"}]}";
}
std::string Changed(std::string body, const std::string& from, const std::string& to)
{
    auto at = body.find(from); if (at == std::string::npos) throw std::runtime_error("test marker missing"); body.replace(at, from.size(), to); return body;
}
Result Wait(Checker& checker)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    Result result;
    while (!checker.TakeResult(result)) { if (std::chrono::steady_clock::now() > deadline) throw std::runtime_error("worker timeout"); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    return result;
}
int main()
{
    try
    {
        std::array<unsigned, 3> version{}, lower{};
        Check(ParseVersion("1.86.4-personal", version), "personal version");
        Check(ParseVersion("v1.86.3-local", lower) && version > lower, "local to personal numeric comparison");
        Check(ParseVersion("1.100.0", version) && ParseVersion("1.86.999", lower) && version > lower, "numeric rather than lexical comparison");
        for (const auto& bad : { "", "v", "1.86", "1.8.6.4", "01.86.4", "1.086.4", "1.86.4-preview", "1.86.4-personal/other", "1.86.4 ", "1.86.-4", "1.65536.4", "999999999999.1.1" })
            Check(!ParseVersion(bad, version), "invalid version rejected");
        auto valid = Release();
        Check(ParseRelease({200, valid}, "1.86.4-personal").status == Status::Available, "published x64 update");
        Check(ParseRelease({200, Release("1.86.4-personal")}, "1.86.4-personal").status == Status::Current, "same version");
        Check(ParseRelease({200, Release("1.86.3-local")}, "1.86.4-personal").status == Status::Current, "no downgrade");
        Check(ParseRelease({200, valid}, "1.86.4-personal").version == "1.86.5-personal", "release notes cannot impersonate top-level tag");
        Check(ParseRelease({403, valid}, "1.86.4-personal").status == Status::Error, "HTTP failure");
        Check(ParseRelease({200, std::string(MaxResponseBytes + 1, ' ')}, "1.86.4-personal").status == Status::Error, "response bound");
        for (const auto& bad : {
            Changed(valid, "\"draft\":false", "\"draft\":true"),
            Changed(valid, "\"prerelease\":false", "\"prerelease\":true"),
            Changed(valid, "\"draft\":false", "\"draft\":\"false\""),
            Changed(valid, "\"published_at\":\"2026-10-15T00:00:00Z\"", "\"published_at\":\"\""),
            Changed(valid, "ganwennl/traffic-monitor/releases/tag", "zhongyang219/TrafficMonitor/releases/tag"),
            Changed(valid, "ganwennl/traffic-monitor/releases/download", "someone/else/releases/download"),
            Changed(valid, "Windows-x64-Lite-portable.zip", "Windows-arm64-Lite-portable.zip"),
            Changed(valid, "\"state\":\"uploaded\"", "\"state\":\"new\""),
            Changed(valid, "\"size\":1234", "\"size\":0"),
            Changed(valid, "\"size\":1234", "\"size\":1.2"),
            Changed(valid, "\"tag_name\":\"v1.86.5-personal\"", "\"tag_name\":\"v1.86.5-personal\",\"tag_name\":\"v99.0.0\""),
            Changed(valid, "\\u4e2d", "\\uD800"), valid.substr(0, valid.size() - 1), valid + " trailing" })
            Check(ParseRelease({200, bad}, "1.86.4-personal").status == Status::Error, "untrusted release rejected");

        Checker checker;
        Check(checker.Start("1.86.4-personal", true, [valid](const Cancellation&) { return Response{200, valid}; }), "manual worker starts");
        Result result = Wait(checker);
        Check(result.manual && result.status == Status::Available && result.releaseUrl == "https://github.com/ganwennl/traffic-monitor/releases/tag/v1.86.5-personal", "manual result and fixed URL");
        Check(!checker.TakeResult(result), "result consumed once");
        Check(checker.Start("1.86.4-personal", false, [](const Cancellation&) -> Response { throw std::runtime_error("mock failure"); }), "automatic failure starts");
        result = Wait(checker);
        Check(!result.manual && result.status == Status::Error && result.releaseUrl.empty(), "automatic failure carries no UI or raw error");

        struct Gate { std::mutex mutex; std::condition_variable cv; bool entered{}, release{}; Cancellation cancel; std::promise<void> completed; };
        auto gate = std::make_shared<Gate>();
        auto completion = gate->completed.get_future();
        {
            Checker closing;
            Check(closing.Start("1.86.4-personal", false, [gate, valid](const Cancellation& cancel) {
                std::unique_lock<std::mutex> lock(gate->mutex); gate->cancel = cancel; gate->entered = true; gate->cv.notify_all(); gate->cv.wait(lock, [&] { return gate->release; });
                lock.unlock(); gate->completed.set_value(); return Response{200, valid};
            }), "blocked worker starts");
            { std::unique_lock<std::mutex> lock(gate->mutex); Check(gate->cv.wait_for(lock, std::chrono::seconds(2), [&] { return gate->entered; }), "worker entered"); }
            Check(!closing.Start("1.86.4-personal", true, {}), "concurrent request rejected");
            const auto before = std::chrono::steady_clock::now(); closing.Stop();
            Check(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(200), "exit does not block on network");
            Check(gate->cancel->load() && !closing.TakeResult(result), "cancelled result cannot reach window");
        }
        { std::lock_guard<std::mutex> lock(gate->mutex); gate->release = true; gate->cv.notify_all(); }
        Check(completion.wait_for(std::chrono::seconds(2)) == std::future_status::ready, "worker survives owner destruction safely");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::cout << "PASS " << checks << " checks; no UI, network or application launch\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}

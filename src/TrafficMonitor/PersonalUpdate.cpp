#include "PersonalUpdate.h"
#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

namespace PersonalUpdate
{
namespace
{
constexpr char ReleaseRoot[] = "https://github.com/ganwennl/traffic-monitor/releases/";
struct Json
{
    enum Kind { Null, Boolean, Number, String, Object, Array } kind{ Null };
    std::string text;
    bool boolean{};
    std::map<std::string, Json> object;
    std::vector<Json> array;
    const Json& Get(const char* name) const
    {
        if (kind != Object) throw std::runtime_error("object expected");
        auto item = object.find(name);
        if (item == object.end()) throw std::runtime_error("missing field");
        return item->second;
    }
    std::string Text() const
    {
        if (kind != String) throw std::runtime_error("string expected");
        return text;
    }
    bool IsFalse() const { return kind == Boolean && !boolean; }
};

// Parse actual JSON structure; strings in release notes cannot impersonate fields.
class JsonReader
{
public:
    explicit JsonReader(const std::string& text) : m_text(text) {}
    Json Read() { Json value = Value(0); Space(); if (m_pos != m_text.size()) Fail(); return value; }
private:
    const std::string& m_text;
    size_t m_pos{}, m_nodes{};
    [[noreturn]] static void Fail() { throw std::runtime_error("invalid JSON"); }
    void Space() { while (m_pos < m_text.size() && (m_text[m_pos] == ' ' || m_text[m_pos] == '\r' || m_text[m_pos] == '\n' || m_text[m_pos] == '\t')) ++m_pos; }
    bool Eat(char ch) { Space(); if (m_pos < m_text.size() && m_text[m_pos] == ch) { ++m_pos; return true; } return false; }
    unsigned Hex4()
    {
        unsigned value{};
        for (int i = 0; i < 4; ++i)
        {
            if (m_pos == m_text.size()) Fail();
            char ch = m_text[m_pos++];
            unsigned digit = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : 16;
            if (digit == 16) Fail();
            value = value * 16 + digit;
        }
        return value;
    }
    static void Utf8(std::string& out, unsigned cp)
    {
        if (cp < 128) out += static_cast<char>(cp);
        else if (cp < 2048) { out += static_cast<char>(192 | (cp >> 6)); out += static_cast<char>(128 | (cp & 63)); }
        else if (cp < 65536) { out += static_cast<char>(224 | (cp >> 12)); out += static_cast<char>(128 | ((cp >> 6) & 63)); out += static_cast<char>(128 | (cp & 63)); }
        else { out += static_cast<char>(240 | (cp >> 18)); out += static_cast<char>(128 | ((cp >> 12) & 63)); out += static_cast<char>(128 | ((cp >> 6) & 63)); out += static_cast<char>(128 | (cp & 63)); }
    }
    std::string String()
    {
        if (!Eat('"')) Fail();
        std::string out;
        while (m_pos < m_text.size())
        {
            unsigned char ch = static_cast<unsigned char>(m_text[m_pos++]);
            if (ch == '"') return out;
            if (ch < 32) Fail();
            if (ch != '\\')
            {
                if (ch < 128) { out += static_cast<char>(ch); continue; }
                unsigned count = ch >= 0xC2 && ch <= 0xDF ? 1 : ch >= 0xE0 && ch <= 0xEF ? 2 : ch >= 0xF0 && ch <= 0xF4 ? 3 : 0;
                if (!count || m_pos + count > m_text.size()) Fail();
                unsigned cp = ch & ((1u << (6 - count)) - 1);
                for (unsigned n = 0; n < count; ++n) { unsigned char next = static_cast<unsigned char>(m_text[m_pos++]); if ((next & 0xC0) != 0x80) Fail(); cp = (cp << 6) | (next & 63); }
                if (cp < (count == 1 ? 128u : count == 2 ? 2048u : 65536u) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) Fail();
                Utf8(out, cp); continue;
            }
            if (m_pos == m_text.size()) Fail();
            char esc = m_text[m_pos++];
            if (esc == '"' || esc == '\\' || esc == '/') out += esc;
            else if (esc == 'b') out += '\b'; else if (esc == 'f') out += '\f'; else if (esc == 'n') out += '\n'; else if (esc == 'r') out += '\r'; else if (esc == 't') out += '\t';
            else if (esc == 'u')
            {
                unsigned cp = Hex4();
                if (cp >= 0xD800 && cp <= 0xDBFF) { if (m_pos + 2 > m_text.size() || m_text.substr(m_pos, 2) != "\\u") Fail(); m_pos += 2; unsigned low = Hex4(); if (low < 0xDC00 || low > 0xDFFF) Fail(); cp = 0x10000 + ((cp - 0xD800) << 10) + low - 0xDC00; }
                else if (cp >= 0xDC00 && cp <= 0xDFFF) Fail();
                Utf8(out, cp);
            }
            else Fail();
        }
        Fail();
    }
    Json Value(unsigned depth)
    {
        Space(); if (depth > 32 || ++m_nodes > 32768 || m_pos == m_text.size()) Fail();
        Json value;
        if (m_text[m_pos] == '"') { value.kind = Json::String; value.text = String(); return value; }
        if (Eat('{'))
        {
            value.kind = Json::Object; if (Eat('}')) return value;
            do { std::string key = String(); if (!Eat(':')) Fail(); Json child = Value(depth + 1); if (!value.object.emplace(key, std::move(child)).second) Fail(); } while (Eat(','));
            if (!Eat('}')) Fail(); return value;
        }
        if (Eat('[')) { value.kind = Json::Array; if (Eat(']')) return value; do { value.array.push_back(Value(depth + 1)); } while (Eat(',')); if (!Eat(']')) Fail(); return value; }
        for (const auto& literal : { "true", "false", "null" })
        {
            const size_t length = std::char_traits<char>::length(literal);
            if (m_text.compare(m_pos, length, literal) == 0) { m_pos += length; value.kind = *literal == 'n' ? Json::Null : Json::Boolean; value.boolean = *literal == 't'; return value; }
        }
        const size_t start = m_pos;
        if (m_text[m_pos] == '-') ++m_pos;
        if (m_pos == m_text.size() || m_text[m_pos] < '0' || m_text[m_pos] > '9') Fail();
        if (m_text[m_pos] == '0') ++m_pos; else while (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9') ++m_pos;
        if (m_pos < m_text.size() && m_text[m_pos] == '.') { ++m_pos; size_t first = m_pos; while (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9') ++m_pos; if (m_pos == first) Fail(); }
        if (m_pos < m_text.size() && (m_text[m_pos] == 'e' || m_text[m_pos] == 'E')) { ++m_pos; if (m_pos < m_text.size() && (m_text[m_pos] == '+' || m_text[m_pos] == '-')) ++m_pos; size_t first = m_pos; while (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9') ++m_pos; if (m_pos == first) Fail(); }
        value.kind = Json::Number; value.text = m_text.substr(start, m_pos - start); return value;
    }
};

Response FetchLatest(const Cancellation& stop)
{
    Response result;
#ifdef _WIN32
    struct Handle { HINTERNET value{}; ~Handle() { if (value) WinHttpCloseHandle(value); } };
    // All synchronous handles belong to this worker. Stop never closes them
    // across threads: WinHTTP prohibits closing an in-use synchronous handle.
    Handle session{ WinHttpOpen(L"TrafficMonitor-Personal", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) };
    if (!session.value || !WinHttpSetTimeouts(session.value, 3000, 3000, 3000, 3000) || stop->load()) return result;
    Handle connection{ WinHttpConnect(session.value, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0) };
    if (!connection.value || stop->load()) return result;
    Handle request{ WinHttpOpenRequest(connection.value, L"GET", L"/repos/ganwennl/traffic-monitor/releases/latest", nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) };
    DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_AUTHENTICATION | WINHTTP_DISABLE_COOKIES;
    if (!request.value || !WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled))) return result;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    auto cancelled = [&] { return stop->load() || std::chrono::steady_clock::now() >= deadline; };
    if (cancelled() || !WinHttpSendRequest(request.value, L"Accept: application/vnd.github+json\r\n", static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || cancelled()
        || !WinHttpReceiveResponse(request.value, nullptr) || cancelled()) return result;
    DWORD size = sizeof(result.status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &result.status, &size, WINHTTP_NO_HEADER_INDEX) || result.status != 200) return result;
    char bytes[8192]; DWORD received{};
    while (!cancelled())
    {
        if (!WinHttpReadData(request.value, bytes, sizeof(bytes), &received) || cancelled()) return {};
        if (!received) return result;
        if (result.body.size() + received > MaxResponseBytes) return {};
        result.body.append(bytes, received);
    }
#endif
    return {};
}
}

bool ParseVersion(const std::string& input, std::array<unsigned, 3>& numbers)
{
    const std::string text = input.size() && input[0] == 'v' ? input.substr(1) : input;
    const size_t suffix = text.find('-');
    if (suffix != std::string::npos && text.substr(suffix) != "-local" && text.substr(suffix) != "-personal") return false;
    const std::string digits = text.substr(0, suffix);
    size_t position{};
    for (unsigned part = 0; part < 3; ++part)
    {
        size_t start = position; unsigned value{};
        while (position < digits.size() && digits[position] >= '0' && digits[position] <= '9') { value = value * 10 + digits[position++] - '0'; if (value > 65535) return false; }
        if (position == start || (position - start > 1 && digits[start] == '0')) return false;
        numbers[part] = value;
        if (part != 2 && (position == digits.size() || digits[position++] != '.')) return false;
    }
    return position == digits.size();
}

Result ParseRelease(const Response& response, const std::string& currentVersion)
{
    Result result;
    try
    {
        if (response.status != 200 || response.body.empty() || response.body.size() > MaxResponseBytes) return result;
        const Json root = JsonReader(response.body).Read();
        if (!root.Get("draft").IsFalse() || !root.Get("prerelease").IsFalse() || root.Get("published_at").Text().empty()) return result;
        const std::string tag = root.Get("tag_name").Text();
        std::array<unsigned, 3> remote{}, local{};
        if (!ParseVersion(tag, remote) || !ParseVersion(currentVersion, local)) return result;
        const std::string version = tag[0] == 'v' ? tag.substr(1) : tag;
        const std::string page = std::string(ReleaseRoot) + "tag/" + tag;
        if (root.Get("html_url").Text() != page) return result;
        const Json& assets = root.Get("assets");
        if (assets.kind != Json::Array) return result;
        const std::string prefix = "TrafficMonitor-" + version + "-Windows-x64-Lite";
        bool compatible{};
        for (const Json& asset : assets.array)
        {
            const std::string name = asset.Get("name").Text();
            if (name != prefix + "-portable.zip" && name != prefix + ".exe") continue;
            const Json& bytes = asset.Get("size");
            if (asset.Get("state").Text() != "uploaded" || bytes.kind != Json::Number || bytes.text.empty()
                || !std::all_of(bytes.text.begin(), bytes.text.end(), [](char ch) { return ch >= '0' && ch <= '9'; }) || std::stoull(bytes.text) == 0
                || asset.Get("browser_download_url").Text() != std::string(ReleaseRoot) + "download/" + tag + "/" + name) continue;
            compatible = true;
        }
        if (!compatible) return result;
        result.status = remote > local ? Status::Available : Status::Current;
        result.version = version; result.releaseUrl = page;
    }
    catch (...) { result = {}; }
    return result;
}

struct Checker::State
{
    Cancellation cancelled{ std::make_shared<std::atomic_bool>(false) };
    std::mutex mutex;
    bool finished{};
    Result result;
};
Checker::~Checker() { Stop(); }
bool Checker::Running() const
{
    if (!m_state) return false;
    std::lock_guard<std::mutex> lock(m_state->mutex);
    return !m_state->finished;
}
bool Checker::Start(const std::string& currentVersion, bool manual, Fetcher fetcher)
{
    if (Running()) return false;
    if (m_worker.joinable()) m_worker.join();
    m_state = std::make_shared<State>();
    const auto state = m_state;
    if (!fetcher) fetcher = FetchLatest;
    try
    {
        m_worker = std::thread([state, currentVersion, manual, fetcher] {
            Result result;
            try { const Response response = fetcher(state->cancelled); if (!state->cancelled->load()) result = ParseRelease(response, currentVersion); }
            catch (...) { result = {}; }
            result.manual = manual;
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->cancelled->load()) result.status = Status::Cancelled;
            state->result = std::move(result); state->finished = true;
        });
    }
    catch (...) { m_state.reset(); return false; }
    return true;
}
bool Checker::TakeResult(Result& result)
{
    if (!m_state) return false;
    { std::lock_guard<std::mutex> lock(m_state->mutex); if (!m_state->finished) return false; result = m_state->result; }
    if (m_worker.joinable()) m_worker.join();
    m_state.reset(); return true;
}
void Checker::Stop()
{
    if (m_state) m_state->cancelled->store(true);
    // A synchronous operation may still be returning within its short timeout.
    // Its exclusively owned std/WinHTTP state survives independently; no HWND,
    // MFC, language table, callback or `this` is captured by the worker.
    if (m_worker.joinable()) m_worker.detach();
    m_state.reset();
}
}

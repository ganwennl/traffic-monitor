#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "IpRegionLookup.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace
{
    constexpr size_t HeaderBytes = 256;
    constexpr size_t VectorBytes = 256 * 256 * 8;
    constexpr size_t DataStart = HeaderBytes + VectorBytes;
    constexpr size_t MaxDatabaseBytes = 64 * 1024 * 1024;
    constexpr size_t MaxRegionBytes = 4096;
    const wchar_t* Unavailable = L"离线库不可用";

    uint16_t Le16(const unsigned char* p)
    {
        return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
    }

    uint32_t Le32(const unsigned char* p)
    {
        return p[0] | (static_cast<uint32_t>(p[1]) << 8) |
            (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }

    // Reject invalid UTF-8, controls, and unbounded region strings before publishing a database.
    bool ValidRegion(const unsigned char* p, size_t size)
    {
        if (size == 0 || size > MaxRegionBytes)
            return false;
        for (size_t i = 0; i < size;)
        {
            uint32_t value = p[i++];
            if (value < 0x80)
            {
                if (value < 0x20 || value == 0x7f)
                    return false;
                continue;
            }
            size_t following = 0;
            uint32_t minimum = 0;
            if (value >= 0xc2 && value <= 0xdf) { following = 1; minimum = 0x80; value &= 0x1f; }
            else if (value >= 0xe0 && value <= 0xef) { following = 2; minimum = 0x800; value &= 0x0f; }
            else if (value >= 0xf0 && value <= 0xf4) { following = 3; minimum = 0x10000; value &= 0x07; }
            else return false;
            if (following > size - i)
                return false;
            for (size_t j = 0; j < following; ++j)
            {
                if ((p[i] & 0xc0) != 0x80)
                    return false;
                value = (value << 6) | (p[i++] & 0x3f);
            }
            if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff) ||
                (value >= 0x80 && value <= 0x9f))
                return false;
        }
        return true;
    }

    std::wstring DecodeRegion(const unsigned char* p, size_t size)
    {
        const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            reinterpret_cast<const char*>(p), static_cast<int>(size), nullptr, 0);
        if (count <= 0)
            return Unavailable;
        std::wstring decoded(static_cast<size_t>(count), L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            reinterpret_cast<const char*>(p), static_cast<int>(size), &decoded[0], count) != count)
            return Unavailable;
        std::wstring output;
        std::wstring previous;
        size_t start = 0;
        while (start <= decoded.size())
        {
            size_t end = decoded.find(L'|', start);
            if (end == std::wstring::npos)
                end = decoded.size();
            const std::wstring part = decoded.substr(start, end - start);
            if (!part.empty() && part != L"0" && part != L"-" && part != previous)
            {
                if (!output.empty()) output += L" · ";
                output += part;
                previous = part;
            }
            if (end == decoded.size()) break;
            start = end + 1;
        }
        return output.empty() ? L"未知归属地" : output;
    }

    std::wstring DatabasePath(unsigned version)
    {
        std::vector<wchar_t> path(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0 || length >= path.size())
            return {};
        const std::wstring executable(path.data(), length);
        const size_t slash = executable.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
            return {};
        return executable.substr(0, slash + 1) + L"data\\ip2region_v" +
            std::to_wstring(version) + L".xdb";
    }

    struct FileHandle
    {
        HANDLE value;
        ~FileHandle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    };

    class Database
    {
    public:
        explicit Database(unsigned version) noexcept : m_ipBytes(version == 4 ? 4 : 16),
            m_rowBytes(m_ipBytes * 2 + 6)
        {
            try
            {
                const std::wstring path = DatabasePath(version);
                if (path.empty()) return;
                FileHandle file{ CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr) };
                LARGE_INTEGER size{};
                if (file.value == INVALID_HANDLE_VALUE || !GetFileSizeEx(file.value, &size) ||
                    size.QuadPart < static_cast<LONGLONG>(DataStart + m_rowBytes) ||
                    size.QuadPart > static_cast<LONGLONG>(MaxDatabaseBytes))
                    return;
                m_bytes.resize(static_cast<size_t>(size.QuadPart));
                DWORD read = 0;
                if (!ReadFile(file.value, m_bytes.data(), static_cast<DWORD>(m_bytes.size()), &read, nullptr) ||
                    read != m_bytes.size() || !Validate(version))
                {
                    std::vector<unsigned char>().swap(m_bytes);
                    return;
                }
                m_available = true;
            }
            catch (...)
            {
                std::vector<unsigned char>().swap(m_bytes);
            }
        }

        bool Available() const noexcept { return m_available; }

        std::wstring Find(const std::array<unsigned char, 16>& address) const
        {
            if (!Available()) return Unavailable;
            const size_t vector = HeaderBytes + (static_cast<size_t>(address[0]) * 256 + address[1]) * 8;
            const uint32_t first = Le32(m_bytes.data() + vector);
            size_t last = Le32(m_bytes.data() + vector + 4);
            if (first == 0 || last == 0) return L"未知归属地";
            // The upstream generator uses EOF as the final bucket's end sentinel.
            if (last == m_bytes.size()) last -= m_rowBytes;
            size_t low = 0;
            size_t high = (last - first) / m_rowBytes + 1;
            while (low < high)
            {
                const size_t mid = low + (high - low) / 2;
                const unsigned char* row = m_bytes.data() + first + mid * m_rowBytes;
                if (Compare(address.data(), row) < 0) high = mid;
                else if (Compare(address.data(), row + m_ipBytes) > 0) low = mid + 1;
                else
                {
                    const size_t length = Le16(row + m_ipBytes * 2);
                    const size_t pointer = Le32(row + m_ipBytes * 2 + 2);
                    // Validate already checked every record; retain a local read boundary guard.
                    if (pointer > m_bytes.size() || length > m_bytes.size() - pointer)
                        return Unavailable;
                    return DecodeRegion(m_bytes.data() + pointer, length);
                }
            }
            return L"未知归属地";
        }

    private:
        int Compare(const unsigned char* numeric, const unsigned char* stored) const noexcept
        {
            for (size_t i = 0; i < m_ipBytes; ++i)
            {
                const unsigned char other = stored[m_ipBytes == 4 ? 3 - i : i];
                if (numeric[i] < other) return -1;
                if (numeric[i] > other) return 1;
            }
            return 0;
        }

        int CompareStored(const unsigned char* left, const unsigned char* right) const noexcept
        {
            for (size_t i = 0; i < m_ipBytes; ++i)
            {
                const size_t offset = m_ipBytes == 4 ? 3 - i : i;
                if (left[offset] < right[offset]) return -1;
                if (left[offset] > right[offset]) return 1;
            }
            return 0;
        }

        bool Validate(unsigned ipVersion) const noexcept
        {
            const unsigned char* base = m_bytes.data();
            const uint16_t version = Le16(base);
            // XDB 2 is IPv4 only; XDB 3 records IP version and 4-byte pointers explicitly.
            if ((version != 2 && version != 3) || (version == 2 && ipVersion != 4) ||
                (version == 3 && (Le16(base + 16) != ipVersion || Le16(base + 18) != 4)) ||
                Le16(base + 2) != 1)
                return false;
            const size_t first = Le32(base + 8);
            const size_t last = Le32(base + 12);
            if (first < DataStart || first > last || last > m_bytes.size() - m_rowBytes ||
                last + m_rowBytes != m_bytes.size() || (last - first) % m_rowBytes != 0)
                return false;
            for (size_t vector = HeaderBytes; vector < DataStart; vector += 8)
            {
                const size_t start = Le32(base + vector);
                const size_t end = Le32(base + vector + 4);
                if (start == 0 && end == 0) continue;
                if (start < first || start > last || end < start || end > last + m_rowBytes ||
                    (start - first) % m_rowBytes != 0 || (end - first) % m_rowBytes != 0)
                    return false;
            }
            for (size_t offset = first; offset <= last; offset += m_rowBytes)
            {
                const unsigned char* row = base + offset;
                const size_t length = Le16(row + m_ipBytes * 2);
                const size_t pointer = Le32(row + m_ipBytes * 2 + 2);
                if (CompareStored(row, row + m_ipBytes) > 0 ||
                    (offset != first && CompareStored(row - m_rowBytes + m_ipBytes, row) >= 0) ||
                    pointer < DataStart || pointer > first || length > first - pointer ||
                    !ValidRegion(base + pointer, length))
                    return false;
            }
            return true;
        }

        size_t m_ipBytes;
        size_t m_rowBytes;
        std::vector<unsigned char> m_bytes;
        bool m_available{};
    };

    const Database& GetDatabase(unsigned version)
    {
        if (version == 4) { static const Database database(4); return database; }
        static const Database database(6);
        return database;
    }

    bool ParseV4(const std::wstring& text, unsigned char* bytes)
    {
        size_t start = 0;
        for (size_t field = 0; field < 4; ++field)
        {
            const size_t end = field == 3 ? text.size() : text.find(L'.', start);
            if (end == std::wstring::npos || end <= start || end - start > 3) return false;
            unsigned value = 0;
            for (size_t i = start; i < end; ++i)
            {
                if (text[i] < L'0' || text[i] > L'9') return false;
                value = value * 10 + text[i] - L'0';
            }
            if (value > 255 || (end - start > 1 && text[start] == L'0')) return false;
            bytes[field] = static_cast<unsigned char>(value);
            start = end + 1;
        }
        return true;
    }

    bool ParseGroups(const std::wstring& text, std::vector<uint16_t>& groups, bool allowV4)
    {
        if (text.empty()) return true;
        size_t start = 0;
        while (start < text.size())
        {
            size_t end = text.find(L':', start);
            if (end == std::wstring::npos) end = text.size();
            const std::wstring part = text.substr(start, end - start);
            if (part.find(L'.') != std::wstring::npos)
            {
                unsigned char ipv4[4]{};
                if (!allowV4 || end != text.size() || !ParseV4(part, ipv4)) return false;
                groups.push_back(static_cast<uint16_t>(ipv4[0] * 256 + ipv4[1]));
                groups.push_back(static_cast<uint16_t>(ipv4[2] * 256 + ipv4[3]));
            }
            else
            {
                if (part.empty() || part.size() > 4) return false;
                unsigned value = 0;
                for (wchar_t c : part)
                {
                    unsigned digit;
                    if (c >= L'0' && c <= L'9') digit = c - L'0';
                    else if (c >= L'a' && c <= L'f') digit = c - L'a' + 10;
                    else if (c >= L'A' && c <= L'F') digit = c - L'A' + 10;
                    else return false;
                    value = value * 16 + digit;
                }
                groups.push_back(static_cast<uint16_t>(value));
            }
            if (groups.size() > 8 || end + 1 == text.size()) return false;
            start = end + 1;
        }
        return true;
    }

    unsigned ParseAddress(std::wstring text, std::array<unsigned char, 16>& address)
    {
        if (text.empty() || text.size() > 128) return 0;
        if (text.front() == L'[' && text.back() == L']') text = text.substr(1, text.size() - 2);
        const size_t scope = text.find(L'%');
        if (scope != std::wstring::npos)
        {
            if (text.find(L':') == std::wstring::npos || scope + 1 == text.size()) return 0;
            for (size_t i = scope + 1; i < text.size(); ++i)
                if (!((text[i] >= L'0' && text[i] <= L'9') || (text[i] >= L'a' && text[i] <= L'z') ||
                    (text[i] >= L'A' && text[i] <= L'Z') || text[i] == L'-' || text[i] == L'_' || text[i] == L'.'))
                    return 0;
            text.resize(scope);
        }
        if (text.find(L':') == std::wstring::npos)
            return ParseV4(text, address.data()) ? 4 : 0;
        const size_t compression = text.find(L"::");
        std::vector<uint16_t> left, right;
        if (compression == std::wstring::npos)
        {
            if (!ParseGroups(text, left, true) || left.size() != 8) return 0;
        }
        else
        {
            if (text.find(L"::", compression + 2) != std::wstring::npos ||
                !ParseGroups(text.substr(0, compression), left, false) ||
                !ParseGroups(text.substr(compression + 2), right, true) || left.size() + right.size() >= 8)
                return 0;
            left.resize(8 - right.size(), 0);
            left.insert(left.end(), right.begin(), right.end());
        }
        for (size_t i = 0; i < 8; ++i)
        {
            address[i * 2] = static_cast<unsigned char>(left[i] >> 8);
            address[i * 2 + 1] = static_cast<unsigned char>(left[i]);
        }
        // Resolve IPv4-mapped IPv6 through the IPv4 database and local address rules.
        bool mapped = address[10] == 0xff && address[11] == 0xff;
        for (size_t i = 0; i < 10; ++i) mapped = mapped && address[i] == 0;
        if (mapped)
        {
            for (size_t i = 0; i < 4; ++i) address[i] = address[i + 12];
            return 4;
        }
        return 6;
    }

    std::wstring SpecialAddress(const std::array<unsigned char, 16>& a, unsigned version)
    {
        if (version == 4)
        {
            const uint32_t value = (static_cast<uint32_t>(a[0]) << 24) |
                (static_cast<uint32_t>(a[1]) << 16) | (static_cast<uint32_t>(a[2]) << 8) | a[3];
            if (value == 0) return L"未指定地址";
            if (a[0] == 127) return L"本机回环";
            if (a[0] == 10 || (a[0] == 172 && (a[1] & 0xf0) == 16) ||
                (a[0] == 192 && a[1] == 168)) return L"局域网";
            if (a[0] == 169 && a[1] == 254) return L"链路本地";
            if (a[0] == 100 && (a[1] & 0xc0) == 64) return L"运营商共享地址";
            if (value == 0xffffffff) return L"广播地址";
            if (a[0] >= 224 && a[0] < 240) return L"组播地址";
            if ((a[0] == 192 && a[1] == 0 && a[2] == 2) ||
                (a[0] == 198 && a[1] == 51 && a[2] == 100) ||
                (a[0] == 203 && a[1] == 0 && a[2] == 113)) return L"文档示例地址";
            if (a[0] == 0 || a[0] >= 240 || (a[0] == 198 && (a[1] & 0xfe) == 18) ||
                (a[0] == 192 && a[1] == 0 && a[2] == 0)) return L"保留地址";
        }
        else
        {
            bool zeroPrefix = true;
            for (size_t i = 0; i < 15; ++i) zeroPrefix = zeroPrefix && a[i] == 0;
            if (zeroPrefix && a[15] == 0) return L"未指定地址";
            if (zeroPrefix && a[15] == 1) return L"本机回环";
            if ((a[0] & 0xfe) == 0xfc) return L"局域网";
            if (a[0] == 0xfe && (a[1] & 0xc0) == 0x80) return L"链路本地";
            if (a[0] == 0xff) return L"组播地址";
            if ((a[0] == 0x20 && a[1] == 0x01 && a[2] == 0x0d && a[3] == 0xb8) ||
                (a[0] == 0x3f && a[1] == 0xff && (a[2] & 0xf0) == 0)) return L"文档示例地址";
            if (a[0] == 0x01 && a[1] == 0 && a[2] == 0 && a[3] == 0 &&
                a[4] == 0 && a[5] == 0 && a[6] == 0 && a[7] == 0) return L"保留地址";
        }
        return {};
    }
}

std::wstring OfflineIpRegion::Lookup(const std::wstring& numericAddress)
{
    try
    {
        std::array<unsigned char, 16> address{};
        const unsigned version = ParseAddress(numericAddress, address);
        if (version == 0) return L"无效 IP 地址";
        const std::wstring special = SpecialAddress(address, version);
        return special.empty() ? GetDatabase(version).Find(address) : special;
    }
    catch (...) { return Unavailable; }
}

std::wstring OfflineIpRegion::Status()
{
    const bool v4 = GetDatabase(4).Available();
    const bool v6 = GetDatabase(6).Available();
    if (v4 && v6) return L"ip2region 离线库 · IPv4 / IPv6";
    if (v4) return L"ip2region IPv4 · IPv6 离线库不可用";
    if (v6) return L"ip2region IPv6 · IPv4 离线库不可用";
    return L"ip2region 离线库不可用（缺失或损坏）";
}

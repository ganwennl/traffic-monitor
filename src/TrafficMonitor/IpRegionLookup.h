#pragma once

#include <string>

// Numeric addresses only. Queries never use DNS, contact a server, or save an IP.
namespace OfflineIpRegion
{
    std::wstring Lookup(const std::wstring& numericAddress);
    std::wstring Status();
}

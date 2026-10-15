#pragma once

#include <string>
#include <vector>

namespace TrafficMonitorIni
{
inline std::vector<std::wstring> SectionNames(const std::wstring& text, const std::wstring& prefix)
{
    std::vector<std::wstring> names;
    for (size_t line = 0; line < text.size();)
    {
        const size_t next = text.find(L'\n', line);
        const size_t limit = next == std::wstring::npos ? text.size() : next;
        size_t start = line;
        if (start == 0 && text[start] == L'\ufeff')
            ++start;
        if (start < limit && text[start] == L'[' && text.compare(start + 1, prefix.size(), prefix) == 0)
        {
            const size_t end = text.find_first_of(L"]\r\n", start + 1);
            if (end < limit && text[end] == L']' && end >= start + 1 + prefix.size())
                names.push_back(text.substr(start + 1 + prefix.size(), end - start - 1 - prefix.size()));
        }
        // A malformed heading cannot consume the next line or repeat forever.
        if (next == std::wstring::npos)
            break;
        line = next + 1;
    }
    return names;
}
}

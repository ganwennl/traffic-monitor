#include "../IniSectionNames.h"
#include <iostream>
#include <stdexcept>

int main()
{
    try
    {
        const auto check = [](const std::wstring& text, const std::wstring& prefix, const std::vector<std::wstring>& expected)
        {
            if (TrafficMonitorIni::SectionNames(text, prefix) != expected)
                throw std::runtime_error("INI section parsing mismatch");
        };
        check(L"", L"", {});
        check(L"[first]\nkey=value\n[second]\r\n", L"", {L"first", L"second"});
        check(L"\ufeff[first]\r\n", L"", {L"first"});
        check(L"\n[skin_blue]\n[other]\n[skin_red]", L"skin_", {L"blue", L"red"});
        check(L"\n[broken", L"", {});
        check(L"[broken\n[good]\n[also broken\n[next]", L"", {L"good", L"next"});
        check(L"[skin_broken\n[skin_good]", L"skin_", {L"good"});
        check(L"[skin]\n", L"skin_longer", {});
        check(L"value=[not a section]\n [indented]\n[valid]", L"", {L"valid"});
        std::cout << "PASS 9 INI section regression cases\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}

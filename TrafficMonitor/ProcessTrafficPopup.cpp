#include "stdafx.h"
#include "ProcessTrafficPopup.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    UINT DpiForMonitor(HMONITOR monitor)
    {
        // 仅从系统绝对目录加载，避免当前目录同名 DLL 干扰。
        struct MonitorDpiApi
        {
            using Function = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
            HMODULE module{};
            Function query{};

            MonitorDpiApi()
            {
                wchar_t systemDirectory[MAX_PATH + 1]{};
                const UINT length = ::GetSystemDirectoryW(systemDirectory, _countof(systemDirectory));
                if (length != 0 && length < _countof(systemDirectory))
                {
                    const std::wstring path = std::wstring(systemDirectory) + L"\\shcore.dll";
                    module = ::LoadLibraryW(path.c_str());
                    if (module != nullptr)
                        query = reinterpret_cast<Function>(::GetProcAddress(module, "GetDpiForMonitor"));
                }
            }

            ~MonitorDpiApi()
            {
                if (module != nullptr)
                    ::FreeLibrary(module);
            }
        };
        static const MonitorDpiApi api;
        UINT dpiX{}, dpiY{};
        if (api.query != nullptr && SUCCEEDED(api.query(monitor, 0, &dpiX, &dpiY)) && dpiX != 0)
            return dpiX;

        // Windows 7 没有 GetDpiForMonitor，使用屏幕 DC 的系统 DPI。
        const HDC screenDc = ::GetDC(nullptr);
        const int dpi = screenDc == nullptr ? 96 : ::GetDeviceCaps(screenDc, LOGPIXELSX);
        if (screenDc != nullptr)
            ::ReleaseDC(nullptr, screenDc);
        return dpi > 0 ? static_cast<UINT>(dpi) : 96;
    }
}
BEGIN_MESSAGE_MAP(CProcessTrafficPopup, CWnd)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_MOUSEACTIVATE()
    ON_WM_MOUSEMOVE()
    ON_WM_LBUTTONDOWN()
    ON_WM_SETCURSOR()
    ON_WM_TIMER()
    ON_WM_DESTROY()
    ON_WM_SETTINGCHANGE()
    ON_WM_SYSCOLORCHANGE()
    ON_MESSAGE(WM_DPICHANGED, &CProcessTrafficPopup::OnDpiChanged)
END_MESSAGE_MAP()

CProcessTrafficPopup::~CProcessTrafficPopup()
{
    if (::IsWindow(GetSafeHwnd()))
        DestroyWindow();
}

BOOL CProcessTrafficPopup::Create(CWnd* owner)
{
    if (::IsWindow(GetSafeHwnd()))
        return TRUE;

    m_owner = owner == nullptr ? nullptr : owner->GetSafeHwnd();
    const CString windowClass = AfxRegisterWndClass(CS_DROPSHADOW, ::LoadCursor(nullptr, IDC_ARROW));
    if (!CreateEx(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        windowClass, L"TrafficMonitor 程序流量", WS_POPUP,
        CRect(0, 0, 0, 0), owner, 0))
        return FALSE;

    ApplyDpi(96);
    RefreshAppearance();
    UpdateAccessibleText();
    return TRUE;
}

void CProcessTrafficPopup::SetData(const Snapshot& snapshot)
{
    m_data = snapshot;
    std::stable_sort(m_data.rows.begin(), m_data.rows.end(), [](const Row& left, const Row& right)
    {
        const auto leftRate = TotalRate(left);
        const auto rightRate = TotalRate(right);
        if (leftRate != rightRate)
            return leftRate > rightRate;
        if (left.totalBytes != right.totalBytes)
            return left.totalBytes > right.totalBytes;
        return left.pid < right.pid;
    });
    if (m_data.rows.size() > MaxRows)
        m_data.rows.resize(MaxRows);

    UpdateAccessibleText();

    if (::IsWindow(GetSafeHwnd()))
    {
        if (IsWindowVisible())
            PositionWindow(false);
        Invalidate(FALSE);
    }
}

void CProcessTrafficPopup::ShowAt(CPoint screenPoint)
{
    ShowAt(CRect(screenPoint.x - 16, screenPoint.y - 16, screenPoint.x + 16, screenPoint.y + 16));
}

void CProcessTrafficPopup::ShowAt(const CRect& trayIconRect)
{
    if (!::IsWindow(GetSafeHwnd()))
        return;

    m_anchorRect = trayIconRect;
    m_anchorRect.NormalizeRect();
    if (m_anchorRect.IsRectEmpty())
    {
        const CPoint anchor = m_anchorRect.TopLeft();
        m_anchorRect = CRect(anchor.x - 16, anchor.y - 16, anchor.x + 16, anchor.y + 16);
    }
    m_outside = false;
    m_enableHovered = false;
    RefreshAppearance();
    PositionWindow(true);
    SetTimer(TrackingTimer, 150, nullptr);
    Invalidate(FALSE);
}

void CProcessTrafficPopup::RequestHide()
{
    if (!::IsWindow(GetSafeHwnd()) || !IsWindowVisible())
        return;

    CPoint cursor;
    if (::GetCursorPos(&cursor) && IsPointerInside(cursor))
        m_outside = false;
    else if (!m_outside)
    {
        m_outside = true;
        m_outsideSince = ::GetTickCount();
    }
}

void CProcessTrafficPopup::Hide()
{
    if (::IsWindow(GetSafeHwnd()))
    {
        KillTimer(TrackingTimer);
        ShowWindow(SW_HIDE);
    }
    m_outside = false;
    m_enableHovered = false;
}

int CProcessTrafficPopup::Scale(int pixels) const
{
    return ::MulDiv(pixels, static_cast<int>(m_dpi), 96);
}

void CProcessTrafficPopup::ApplyDpi(UINT dpi)
{
    dpi = (std::max)(72U, (std::min)(768U, dpi));
    if (dpi == m_dpi && m_bodyFont.GetSafeHandle() != nullptr)
        return;

    m_dpi = dpi;
    m_bodyFont.DeleteObject();
    m_titleFont.DeleteObject();
    m_smallFont.DeleteObject();

    NONCLIENTMETRICS metrics{};
    metrics.cbSize = sizeof(metrics);
    LOGFONT font{};
    if (::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
        font = metrics.lfMessageFont;
    else
        wcscpy_s(font.lfFaceName, L"Microsoft YaHei UI");
    font.lfWidth = 0;
    // 固定使用 UI 字体，避免中文回退成宋体而与数字、英文风格不一致。
    wcscpy_s(font.lfFaceName, L"Microsoft YaHei UI");
    font.lfHeight = -Scale(13);
    font.lfWeight = FW_NORMAL;
    font.lfQuality = CLEARTYPE_QUALITY;
    m_bodyFont.CreateFontIndirect(&font);
    font.lfHeight = -Scale(16);
    font.lfWeight = FW_SEMIBOLD;
    m_titleFont.CreateFontIndirect(&font);
    font.lfHeight = -Scale(11);
    font.lfWeight = FW_NORMAL;
    m_smallFont.CreateFontIndirect(&font);
}

void CProcessTrafficPopup::RefreshAppearance()
{
    HIGHCONTRAST contrast{};
    contrast.cbSize = sizeof(contrast);
    m_highContrast = ::SystemParametersInfo(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0)
        && (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
    DWORD lightTheme = 1;
    DWORD bytes = sizeof(lightTheme);
    if (::RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &lightTheme, &bytes) != ERROR_SUCCESS)
        lightTheme = 1;
    m_darkTheme = lightTheme == 0;
}

void CProcessTrafficPopup::UpdateWindowRegion(int width, int height)
{
    if (m_highContrast)
    {
        SetWindowRgn(nullptr, FALSE);
        return;
    }
    CRgn rounded;
    if (rounded.CreateRoundRectRgn(0, 0, width + 1, height + 1, Scale(20), Scale(20))
        && SetWindowRgn(static_cast<HRGN>(rounded.GetSafeHandle()), FALSE))
        rounded.Detach(); // 成功后区域归窗口所有。
}

void CProcessTrafficPopup::PositionWindow(bool show)
{
    if (m_positioning || !::IsWindow(GetSafeHwnd()))
        return;
    m_positioning = true;

    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    const HMONITOR screen = ::MonitorFromRect(&m_anchorRect, MONITOR_DEFAULTTONEAREST);
    ApplyDpi(DpiForMonitor(screen));
    CRect workArea;
    if (::GetMonitorInfo(screen, &monitor))
        workArea = monitor.rcWork;
    else
        ::SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
    if (workArea.IsRectEmpty())
    {
        m_positioning = false;
        return;
    }

    const int gap = Scale(8);
    const int availableWidth = (std::max)(1, workArea.Width() - gap * 2);
    const int availableHeight = (std::max)(1, workArea.Height() - gap * 2);
    const int width = (std::min)(Scale(600), availableWidth);
    const int rowHeight = (std::max)(1, Scale(30));
    const int baseHeight = Scale(210 + (m_data.extraSummary.empty() ? 0 : 48) + (HasSystemTraffic() ? 22 : 0));
    const int rowCapacity = (std::max)(0, (availableHeight - baseHeight) / rowHeight);
    m_visibleRows = (std::min)(m_data.rows.size(), static_cast<size_t>(rowCapacity));
    const int bodyHeight = m_data.rows.empty() ? Scale(64) : static_cast<int>(m_visibleRows) * rowHeight;
    const int height = (std::min)(baseHeight + bodyHeight, availableHeight);

    int x = m_anchorRect.right - width;
    int y = m_anchorRect.top - height - gap;
    if (y < workArea.top + gap)
        y = m_anchorRect.bottom + gap;
    x = (std::max<int>)(workArea.left + gap, (std::min<int>)(x, workArea.right - width - gap));
    y = (std::max<int>)(workArea.top + gap, (std::min<int>)(y, workArea.bottom - height - gap));
    SetWindowPos(&wndTopMost, x, y, width, height,
        SWP_NOACTIVATE | SWP_NOOWNERZORDER | (show ? SWP_SHOWWINDOW : 0));
    UpdateWindowRegion(width, height);
    m_positioning = false;
}

std::uint64_t CProcessTrafficPopup::TotalRate(const Row& row)
{
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    if (maximum - row.uploadBytesPerSecond < row.downloadBytesPerSecond)
        return maximum;
    return row.uploadBytesPerSecond + row.downloadBytesPerSecond;
}

CString CProcessTrafficPopup::FormatBytes(std::uint64_t bytes, bool perSecond)
{
    static const wchar_t* units[] = { L"B", L"KB", L"MB", L"GB", L"TB", L"PB", L"EB" };
    size_t unit = 0;
    double value = static_cast<double>(bytes);
    while (value >= 1024.0 && unit + 1 < _countof(units))
    {
        value /= 1024.0;
        ++unit;
    }
    CString text;
    if (unit == 0)
        text.Format(L"%llu B", static_cast<unsigned long long>(bytes));
    else
        text.Format(L"%.*f %s", value >= 100.0 ? 0 : (value >= 10.0 ? 1 : 2), value, units[unit]);
    if (perSecond)
        text += L"/s";
    return text;
}

CString CProcessTrafficPopup::DisplayName(const Row& row)
{
    // System 无法像普通进程一样查询映像；也兼容仍返回“PID 4”的旧采集进程。
    if (row.pid == 4)
        return L"系统（System）";
    // 输入只需进程名；即使调用者误传完整路径，也只展示最后一段名称。
    const size_t lastSeparator = row.name.find_last_of(L"/\\");
    CString name((lastSeparator == std::wstring::npos ? row.name : row.name.substr(lastSeparator + 1)).c_str());
    name.Replace(L'\r', L' ');
    name.Replace(L'\n', L' ');
    name.Replace(L'\t', L' ');
    return name.IsEmpty() ? CString(L"未知程序") : name.Left(96);
}

bool CProcessTrafficPopup::HasSystemTraffic() const
{
    return std::any_of(m_data.rows.begin(), m_data.rows.end(), [](const Row& row) { return row.pid == 4; });
}

CString CProcessTrafficPopup::NameForPid(DWORD pid) const
{
    for (const auto& row : m_data.rows)
        if (row.pid == pid)
            return DisplayName(row);
    CString name;
    name.Format(L"程序 %lu", static_cast<unsigned long>(pid));
    return name;
}

const CProcessTrafficPopup::Row* CProcessTrafficPopup::RowAt(CPoint point) const
{
    CRect rect;
    GetClientRect(&rect);
    const int top = Scale(164 + (m_data.extraSummary.empty() ? 0 : 48));
    const int height = (std::max)(1, Scale(30));
    if (point.x < Scale(10) || point.x >= rect.right - Scale(10) || point.y < top)
        return nullptr;
    const auto index = static_cast<size_t>((point.y - top) / height);
    return index < m_visibleRows && index < m_data.rows.size() ? &m_data.rows[index] : nullptr;
}

void CProcessTrafficPopup::UpdateAccessibleText()
{
    if (!::IsWindow(GetSafeHwnd()))
        return;

    CString summary = L"TrafficMonitor 程序流量\r\n总下载 " + FormatBytes(m_data.downloadBytesPerSecond, true);
    summary += L"，总上传 " + FormatBytes(m_data.uploadBytesPerSecond, true);
    summary += L"，CPU " + FormatUsage(m_data.cpuUsagePercent);
    summary += L"，内存 " + FormatUsage(m_data.memoryUsagePercent) + L"\r\n";
    if (!m_data.extraSummary.empty())
        summary += CString(m_data.extraSummary.substr(0, 512).c_str()) + L"\r\n";
    summary += CString(m_data.status.substr(0, 256).c_str());
    summary += L"\r\n所有连接 · TCP/UDP · 本次累计自采集启动\r\n点击程序查看当前 TCP 连接 IP 与归属地；标题右侧可单独查询 IP\r\n";
    if (HasSystemTraffic())
        summary += L"System：系统及内核网络流量，可能包含代应用收发的数据\r\n";
    if (m_data.canEnable)
        summary += L"点击允许程序流量统计\r\n";
    if (m_data.rows.empty())
        summary += L"暂无程序流量数据";
    for (const auto& row : m_data.rows)
    {
        CString details;
        details.Format(L"%s（PID %lu）：下载 %s，上传 %s，本次流量 %s\r\n",
            DisplayName(row).GetString(), static_cast<unsigned long>(row.pid),
            FormatBytes(row.downloadBytesPerSecond, true).GetString(),
            FormatBytes(row.uploadBytesPerSecond, true).GetString(),
            FormatBytes(row.totalBytes).GetString());
        summary += details;
    }
    CString previous;
    GetWindowText(previous);
    if (previous != summary)
        SetWindowText(summary);
}
CString CProcessTrafficPopup::FormatUsage(double usage)
{
    if (!std::isfinite(usage) || usage < 0.0)
        return L"--";
    CString text;
    text.Format(L"%.0f%%", (std::min)(100.0, usage));
    return text;
}

void CProcessTrafficPopup::DrawContent(CDC& dc, const CRect& clientRect)
{
    const int saved = dc.SaveDC();
    const auto color = [&](COLORREF light, COLORREF dark, int systemColor)
    {
        return m_highContrast ? ::GetSysColor(systemColor) : (m_darkTheme ? dark : light);
    };
    const COLORREF background = color(RGB(237, 242, 248), RGB(30, 36, 46), COLOR_WINDOW);
    const COLORREF surface = color(RGB(247, 249, 252), RGB(39, 47, 59), COLOR_WINDOW);
    const COLORREF foreground = color(RGB(38, 51, 69), RGB(229, 235, 244), COLOR_WINDOWTEXT);
    const COLORREF secondary = color(RGB(89, 105, 125), RGB(166, 180, 199), COLOR_WINDOWTEXT);
    const COLORREF separator = color(RGB(211, 222, 235), RGB(65, 77, 94), COLOR_WINDOWTEXT);
    const COLORREF accent = color(RGB(55, 101, 156), RGB(145, 187, 238), COLOR_WINDOWTEXT);
    const COLORREF uploadAccent = color(RGB(42, 113, 110), RGB(129, 200, 186), COLOR_WINDOWTEXT);
    const COLORREF activeRow = color(RGB(224, 235, 247), RGB(45, 62, 82), COLOR_WINDOW);
    dc.FillSolidRect(clientRect, background);
    CPen border(PS_SOLID, (std::max)(1, Scale(1)), separator);
    CBrush fill(background);
    CPen* oldPen = dc.SelectObject(&border);
    CBrush* oldBrush = dc.SelectObject(&fill);
    CRect outline(clientRect);
    outline.DeflateRect(0, 0, 1, 1);
    dc.RoundRect(outline, CPoint(Scale(m_highContrast ? 0 : 20), Scale(m_highContrast ? 0 : 20)));
    dc.SelectObject(oldPen);
    dc.SelectObject(oldBrush);
    dc.SetBkMode(TRANSPARENT);
    dc.SetTextColor(foreground);

    CFont* bodyFont = m_bodyFont.GetSafeHandle() == nullptr
        ? CFont::FromHandle(static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT))) : &m_bodyFont;
    CFont* titleFont = m_titleFont.GetSafeHandle() == nullptr ? bodyFont : &m_titleFont;
    CFont* smallFont = m_smallFont.GetSafeHandle() == nullptr ? bodyFont : &m_smallFont;
    const int padding = Scale(16);
    const int right = clientRect.right - padding;
    const int innerWidth = (std::max)(0, right - padding);
    const UINT textFlags = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX;
    auto text = [&](const CString& value, CRect rect, COLORREF color, CFont* font, UINT align = DT_LEFT)
    {
        dc.SelectObject(font);
        dc.SetTextColor(color);
        dc.DrawText(value, rect, textFlags | align);
    };
    auto panel = [&](CRect rect, COLORREF panelColor)
    {
        CPen pen(PS_SOLID, 1, panelColor);
        CBrush brush(panelColor);
        CPen* previousPen = dc.SelectObject(&pen);
        CBrush* previousBrush = dc.SelectObject(&brush);
        dc.RoundRect(rect, CPoint(Scale(m_highContrast ? 0 : 12), Scale(m_highContrast ? 0 : 12)));
        dc.SelectObject(previousPen);
        dc.SelectObject(previousBrush);
    };

    m_lookupRect = CRect((std::max)(padding, right - Scale(100)), Scale(14), right, Scale(37));
    text(L"程序流量排行榜", CRect(padding, Scale(14), m_lookupRect.left - Scale(8), Scale(37)), foreground, titleFont);
    text(L"查询 IP / 归属地", m_lookupRect, accent, smallFont, DT_RIGHT);
    const int middle = padding + innerWidth / 2;
    panel(CRect(padding - Scale(4), Scale(42), middle - Scale(6), Scale(85)), surface);
    panel(CRect(middle - Scale(2), Scale(42), right + Scale(4), Scale(85)), surface);
    CString download = L"总下载  " + FormatBytes(m_data.downloadBytesPerSecond, true);
    CString upload = L"总上传  " + FormatBytes(m_data.uploadBytesPerSecond, true);
    text(download, CRect(padding, Scale(45), middle - Scale(8), Scale(65)), accent, bodyFont);
    text(upload, CRect(middle + Scale(6), Scale(45), right, Scale(65)), uploadAccent, bodyFont);
    CString cpu = L"CPU  " + FormatUsage(m_data.cpuUsagePercent);
    CString memory = L"内存  " + FormatUsage(m_data.memoryUsagePercent);
    text(cpu, CRect(padding, Scale(67), middle - Scale(8), Scale(83)), secondary, smallFont);
    text(memory, CRect(middle + Scale(6), Scale(67), right, Scale(83)), secondary, smallFont);
    const int detailsOffset = m_data.extraSummary.empty() ? 0 : 48;
    if (detailsOffset != 0)
    {
        dc.SelectObject(smallFont);
        dc.SetTextColor(secondary);
        CRect detailsRect(padding, Scale(89), right, Scale(130));
        dc.DrawText(CString(m_data.extraSummary.substr(0, 512).c_str()), detailsRect,
            DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    dc.FillSolidRect(padding, Scale(87 + detailsOffset), innerWidth, (std::max)(1, Scale(1)), separator);

    const CString status = m_data.status.empty()
        ? CString(m_data.canEnable ? L"尚未启用程序流量统计" : L"所有连接 · TCP/UDP · 本次累计自采集启动")
        : CString(m_data.status.c_str());
    m_enableRect.SetRectEmpty();
    if (m_data.canEnable)
    {
        text(status, CRect(padding, Scale(93 + detailsOffset), right, Scale(110 + detailsOffset)), secondary, smallFont);
        m_enableRect = CRect(padding, Scale(110 + detailsOffset), right, Scale(134 + detailsOffset));
        text(L"点击允许程序流量统计", m_enableRect, accent, bodyFont);
        if (m_enableHovered)
            dc.FillSolidRect(padding, Scale(133 + detailsOffset), (std::min)(innerWidth, Scale(166)), (std::max)(1, Scale(1)), accent);
    }
    else
    {
        dc.SelectObject(bodyFont);
        dc.SetTextColor(secondary);
        CRect statusRect(padding, Scale(95 + detailsOffset), right, Scale(134 + detailsOffset));
        dc.DrawText(status, statusRect, DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    const int nameRight = padding + innerWidth * 40 / 100;
    const int downloadRight = nameRight + innerWidth * 18 / 100;
    const int uploadRight = downloadRight + innerWidth * 18 / 100;
    const int columnGap = Scale(10);
    const int headerTop = Scale(141 + detailsOffset);
    const int bodyTop = Scale(164 + detailsOffset);
    text(L"程序（PID）", CRect(padding, headerTop, nameRight - columnGap, bodyTop - Scale(5)), secondary, smallFont);
    text(L"下载", CRect(nameRight, headerTop, downloadRight - columnGap, bodyTop - Scale(5)), secondary, smallFont, DT_RIGHT);
    text(L"上传", CRect(downloadRight, headerTop, uploadRight - columnGap, bodyTop - Scale(5)), secondary, smallFont, DT_RIGHT);
    text(L"本次流量", CRect(uploadRight, headerTop, right, bodyTop - Scale(5)), secondary, smallFont, DT_RIGHT);

    const int rowHeight = Scale(30);
    if (m_data.rows.empty())
    {
        text(m_data.canEnable ? L"允许统计后显示程序流量" : L"暂无程序流量数据",
            CRect(padding, bodyTop, right, bodyTop + Scale(64)), secondary, bodyFont, DT_CENTER);
    }
    else
    {
        for (size_t i = 0; i < m_visibleRows; ++i)
        {
            const Row& row = m_data.rows[i];
            const int top = bodyTop + static_cast<int>(i) * rowHeight;
            const int bottom = top + rowHeight;
            if (i == 0 && TotalRate(row) != 0)
                panel(CRect(padding - Scale(6), top + Scale(1), right + Scale(6), bottom - Scale(1)), activeRow);
            else if (i % 2 == 1)
                panel(CRect(padding - Scale(6), top + Scale(1), right + Scale(6), bottom - Scale(1)), surface);
            const CString name = DisplayName(row);
            CString pid;
            pid.Format(L"%lu", static_cast<unsigned long>(row.pid));
            const int pidWidth = (std::min)(Scale(56), (std::max)(0, (nameRight - padding) / 3));
            text(name, CRect(padding, top, nameRight - columnGap - pidWidth, bottom), foreground, bodyFont);
            text(pid, CRect(nameRight - columnGap - pidWidth, top, nameRight - columnGap, bottom), secondary, smallFont, DT_RIGHT);
            text(FormatBytes(row.downloadBytesPerSecond, true), CRect(nameRight, top, downloadRight - columnGap, bottom), row.downloadBytesPerSecond ? accent : secondary, bodyFont, DT_RIGHT);
            text(FormatBytes(row.uploadBytesPerSecond, true), CRect(downloadRight, top, uploadRight - columnGap, bottom), row.uploadBytesPerSecond ? uploadAccent : secondary, bodyFont, DT_RIGHT);
            text(FormatBytes(row.totalBytes), CRect(uploadRight, top, right, bottom), foreground, bodyFont, DT_RIGHT);
        }
    }

    const int footerTop = clientRect.bottom - Scale(34 + (HasSystemTraffic() ? 22 : 0));
    dc.FillSolidRect(padding, footerTop - Scale(6), innerWidth, (std::max)(1, Scale(1)), separator);
    CString footer = L"点击程序查看连接 · 按当前速率排序 · 每秒刷新";
    if (m_visibleRows < m_data.rows.size())
        footer.Format(L"按当前速率排序 · 当前显示 %u 个程序", static_cast<unsigned>(m_visibleRows));
    text(footer, CRect(padding, footerTop, right, footerTop + Scale(13)), secondary, smallFont);
    text(L"所有连接 · TCP/UDP · 本次累计自采集启动",
        CRect(padding, footerTop + Scale(14), right, footerTop + Scale(27)), secondary, smallFont);
    if (HasSystemTraffic())
        text(L"System：系统及内核网络流量，可能包含代应用收发的数据",
            CRect(padding, footerTop + Scale(30), right, clientRect.bottom - Scale(7)), secondary, smallFont);
    dc.RestoreDC(saved);
}

void CProcessTrafficPopup::OnPaint()
{
    CPaintDC dc(this);
    CRect clientRect;
    GetClientRect(&clientRect);
    if (clientRect.IsRectEmpty())
        return;

    CDC buffer;
    CBitmap bitmap;
    if (!buffer.CreateCompatibleDC(&dc) || !bitmap.CreateCompatibleBitmap(&dc, clientRect.Width(), clientRect.Height()))
    {
        DrawContent(dc, clientRect);
        return;
    }
    CBitmap* oldBitmap = buffer.SelectObject(&bitmap);
    DrawContent(buffer, clientRect);
    dc.BitBlt(0, 0, clientRect.Width(), clientRect.Height(), &buffer, 0, 0, SRCCOPY);
    buffer.SelectObject(oldBitmap);
}

BOOL CProcessTrafficPopup::OnEraseBkgnd(CDC*)
{
    return TRUE;
}

int CProcessTrafficPopup::OnMouseActivate(CWnd*, UINT, UINT)
{
    return MA_NOACTIVATE;
}

void CProcessTrafficPopup::OnMouseMove(UINT, CPoint point)
{
    const bool hovered = m_data.canEnable && m_enableRect.PtInRect(point);
    if (hovered != m_enableHovered)
    {
        m_enableHovered = hovered;
        InvalidateRect(m_enableRect, FALSE);
    }
}

void CProcessTrafficPopup::OnLButtonDown(UINT, CPoint point)
{
    if (m_data.canEnable && m_enableRect.PtInRect(point) && ::IsWindow(m_owner))
        ::PostMessage(m_owner, EnableMessage, 0, 0);
    else if (::IsWindow(m_owner))
    {
        if (m_lookupRect.PtInRect(point))
            ::PostMessage(m_owner, ConnectionsMessage, 0, 0);
        else if (const Row* row = RowAt(point))
            ::PostMessage(m_owner, ConnectionsMessage, row->pid, 0);
    }
}

BOOL CProcessTrafficPopup::OnSetCursor(CWnd*, UINT, UINT)
{
    CPoint cursor;
    ::GetCursorPos(&cursor);
    ScreenToClient(&cursor);
    const bool link = (m_data.canEnable && m_enableRect.PtInRect(cursor))
        || m_lookupRect.PtInRect(cursor) || RowAt(cursor) != nullptr;
    ::SetCursor(::LoadCursor(nullptr, link ? IDC_HAND : IDC_ARROW));
    return TRUE;
}

bool CProcessTrafficPopup::IsPointerInside(CPoint screenPoint) const
{
    CRect popupRect;
    GetWindowRect(&popupRect);
    return m_anchorRect.PtInRect(screenPoint) || popupRect.PtInRect(screenPoint);
}

void CProcessTrafficPopup::OnTimer(UINT_PTR timerId)
{
    if (timerId != TrackingTimer)
    {
        CWnd::OnTimer(timerId);
        return;
    }

    CPoint cursor;
    if (::GetCursorPos(&cursor) && IsPointerInside(cursor))
        m_outside = false;
    else if (!m_outside)
    {
        m_outside = true;
        m_outsideSince = ::GetTickCount();
    }
    else if (static_cast<DWORD>(::GetTickCount() - m_outsideSince) >= 500)
        Hide();
}

void CProcessTrafficPopup::OnDestroy()
{
    KillTimer(TrackingTimer);
    m_owner = nullptr;
    CWnd::OnDestroy();
}

void CProcessTrafficPopup::OnSettingChange(UINT flags, LPCTSTR section)
{
    CWnd::OnSettingChange(flags, section);
    RefreshAppearance();
    if (IsWindowVisible())
        PositionWindow(false);
    Invalidate(FALSE);
}

void CProcessTrafficPopup::OnSysColorChange()
{
    CWnd::OnSysColorChange();
    RefreshAppearance();
    if (IsWindowVisible())
        PositionWindow(false);
    Invalidate(FALSE);
}

LRESULT CProcessTrafficPopup::OnDpiChanged(WPARAM wParam, LPARAM)
{
    const UINT dpi = LOWORD(wParam);
    if (dpi != 0)
        ApplyDpi(dpi);
    PositionWindow(false);
    Invalidate(FALSE);
    return 0;
}

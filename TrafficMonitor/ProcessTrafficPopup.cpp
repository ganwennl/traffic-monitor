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
    const int rowCapacity = (std::max)(0, (availableHeight - Scale(210)) / rowHeight);
    m_visibleRows = (std::min)(m_data.rows.size(), static_cast<size_t>(rowCapacity));
    const int bodyHeight = m_data.rows.empty() ? Scale(64) : static_cast<int>(m_visibleRows) * rowHeight;
    const int height = (std::min)(Scale(210) + bodyHeight, availableHeight);

    int x = m_anchorRect.right - width;
    int y = m_anchorRect.top - height - gap;
    if (y < workArea.top + gap)
        y = m_anchorRect.bottom + gap;
    x = (std::max<int>)(workArea.left + gap, (std::min<int>)(x, workArea.right - width - gap));
    y = (std::max<int>)(workArea.top + gap, (std::min<int>)(y, workArea.bottom - height - gap));
    SetWindowPos(&wndTopMost, x, y, width, height,
        SWP_NOACTIVATE | SWP_NOOWNERZORDER | (show ? SWP_SHOWWINDOW : 0));
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
    // 输入只需进程名；即使调用者误传完整路径，也只展示最后一段名称。
    const size_t lastSeparator = row.name.find_last_of(L"/\\");
    CString name((lastSeparator == std::wstring::npos ? row.name : row.name.substr(lastSeparator + 1)).c_str());
    name.Replace(L'\r', L' ');
    name.Replace(L'\n', L' ');
    name.Replace(L'\t', L' ');
    return name.IsEmpty() ? CString(L"未知程序") : name.Left(96);
}

void CProcessTrafficPopup::UpdateAccessibleText()
{
    if (!::IsWindow(GetSafeHwnd()))
        return;

    CString summary = L"TrafficMonitor 程序流量\r\n总下载 " + FormatBytes(m_data.downloadBytesPerSecond, true);
    summary += L"，总上传 " + FormatBytes(m_data.uploadBytesPerSecond, true);
    summary += L"，CPU " + FormatUsage(m_data.cpuUsagePercent);
    summary += L"，内存 " + FormatUsage(m_data.memoryUsagePercent) + L"\r\n";
    summary += CString(m_data.status.substr(0, 256).c_str());
    summary += L"\r\n所有连接 · TCP/UDP · 本次累计自采集启动\r\n";
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
    const COLORREF background = ::GetSysColor(COLOR_WINDOW);
    const COLORREF foreground = ::GetSysColor(COLOR_WINDOWTEXT);
    const COLORREF secondary = ::GetSysColor(COLOR_GRAYTEXT);
    const COLORREF separator = ::GetSysColor(COLOR_3DLIGHT);
    const COLORREF accent = ::GetSysColor(COLOR_HIGHLIGHT);
    dc.FillSolidRect(clientRect, background);
    dc.Draw3dRect(clientRect, ::GetSysColor(COLOR_WINDOWFRAME), ::GetSysColor(COLOR_WINDOWFRAME));
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

    text(L"程序流量排行榜", CRect(padding, Scale(14), right, Scale(37)), foreground, titleFont);
    const int middle = padding + innerWidth / 2;
    CString download = L"总下载  " + FormatBytes(m_data.downloadBytesPerSecond, true);
    CString upload = L"总上传  " + FormatBytes(m_data.uploadBytesPerSecond, true);
    text(download, CRect(padding, Scale(45), middle - Scale(8), Scale(65)), accent, bodyFont);
    text(upload, CRect(middle, Scale(45), right, Scale(65)), foreground, bodyFont);
    CString cpu = L"CPU  " + FormatUsage(m_data.cpuUsagePercent);
    CString memory = L"内存  " + FormatUsage(m_data.memoryUsagePercent);
    text(cpu, CRect(padding, Scale(67), middle - Scale(8), Scale(83)), secondary, smallFont);
    text(memory, CRect(middle, Scale(67), right, Scale(83)), secondary, smallFont);
    dc.FillSolidRect(padding, Scale(87), innerWidth, (std::max)(1, Scale(1)), separator);

    const CString status = m_data.status.empty()
        ? CString(m_data.canEnable ? L"尚未启用程序流量统计" : L"所有连接 · TCP/UDP · 本次累计自采集启动")
        : CString(m_data.status.c_str());
    m_enableRect.SetRectEmpty();
    if (m_data.canEnable)
    {
        text(status, CRect(padding, Scale(93), right, Scale(110)), secondary, smallFont);
        m_enableRect = CRect(padding, Scale(110), right, Scale(134));
        text(L"点击允许程序流量统计", m_enableRect, accent, bodyFont);
        if (m_enableHovered)
            dc.FillSolidRect(padding, Scale(133), (std::min)(innerWidth, Scale(166)), (std::max)(1, Scale(1)), accent);
    }
    else
    {
        dc.SelectObject(bodyFont);
        dc.SetTextColor(secondary);
        CRect statusRect(padding, Scale(95), right, Scale(134));
        dc.DrawText(status, statusRect, DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    const int nameRight = padding + innerWidth * 40 / 100;
    const int downloadRight = nameRight + innerWidth * 18 / 100;
    const int uploadRight = downloadRight + innerWidth * 18 / 100;
    const int columnGap = Scale(10);
    const int headerTop = Scale(141);
    const int bodyTop = Scale(164);
    text(L"程序（PID）", CRect(padding, headerTop, nameRight - columnGap, bodyTop - Scale(5)), secondary, smallFont);
    text(L"下载", CRect(nameRight, headerTop, downloadRight - columnGap, bodyTop - Scale(5)), secondary, smallFont, DT_RIGHT);
    text(L"上传", CRect(downloadRight, headerTop, uploadRight - columnGap, bodyTop - Scale(5)), secondary, smallFont, DT_RIGHT);
    text(L"本次流量", CRect(uploadRight, headerTop, right, bodyTop - Scale(5)), secondary, smallFont, DT_RIGHT);
    dc.FillSolidRect(padding, bodyTop - Scale(3), innerWidth, (std::max)(1, Scale(1)), separator);

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
            const CString name = DisplayName(row);
            CString pid;
            pid.Format(L"%lu", static_cast<unsigned long>(row.pid));
            const int pidWidth = (std::min)(Scale(56), (std::max)(0, (nameRight - padding) / 3));
            text(name, CRect(padding, top, nameRight - columnGap - pidWidth, bottom), foreground, bodyFont);
            text(pid, CRect(nameRight - columnGap - pidWidth, top, nameRight - columnGap, bottom), secondary, smallFont, DT_RIGHT);
            text(FormatBytes(row.downloadBytesPerSecond, true), CRect(nameRight, top, downloadRight - columnGap, bottom), foreground, bodyFont, DT_RIGHT);
            text(FormatBytes(row.uploadBytesPerSecond, true), CRect(downloadRight, top, uploadRight - columnGap, bottom), foreground, bodyFont, DT_RIGHT);
            text(FormatBytes(row.totalBytes), CRect(uploadRight, top, right, bottom), foreground, bodyFont, DT_RIGHT);
        }
    }

    const int footerTop = clientRect.bottom - Scale(34);
    dc.FillSolidRect(padding, footerTop - Scale(6), innerWidth, (std::max)(1, Scale(1)), separator);
    CString footer = L"按当前速率排序 · 每秒刷新";
    if (m_visibleRows < m_data.rows.size())
        footer.Format(L"按当前速率排序 · 当前显示 %u 个程序", static_cast<unsigned>(m_visibleRows));
    text(footer, CRect(padding, footerTop, right, footerTop + Scale(13)), secondary, smallFont);
    text(L"所有连接 · TCP/UDP · 本次累计自采集启动",
        CRect(padding, footerTop + Scale(14), right, clientRect.bottom - Scale(7)), secondary, smallFont);
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
}

BOOL CProcessTrafficPopup::OnSetCursor(CWnd*, UINT, UINT)
{
    CPoint cursor;
    ::GetCursorPos(&cursor);
    ScreenToClient(&cursor);
    const bool enableLink = m_data.canEnable && m_enableRect.PtInRect(cursor);
    ::SetCursor(::LoadCursor(nullptr, enableLink ? IDC_HAND : IDC_ARROW));
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

LRESULT CProcessTrafficPopup::OnDpiChanged(WPARAM wParam, LPARAM)
{
    const UINT dpi = LOWORD(wParam);
    if (dpi != 0)
        ApplyDpi(dpi);
    PositionWindow(false);
    Invalidate(FALSE);
    return 0;
}

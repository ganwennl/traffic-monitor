#include "stdafx.h"
#include "ProcessConnectionsWindow.h"
#include "ProcessConnections.h"
#include "IpRegionLookup.h"
#include <chrono>

BEGIN_MESSAGE_MAP(CProcessConnectionsWindow, CWnd)
    ON_WM_CREATE()
    ON_WM_SIZE()
    ON_WM_GETMINMAXINFO()
    ON_WM_CLOSE()
    ON_WM_DESTROY()
    ON_WM_TIMER()
    ON_WM_CTLCOLOR()
    ON_WM_ERASEBKGND()
    ON_WM_SETTINGCHANGE()
    ON_WM_SYSCOLORCHANGE()
    ON_COMMAND(QueryId, &CProcessConnectionsWindow::OnQuery)
    ON_COMMAND(RefreshId, &CProcessConnectionsWindow::OnRefresh)
    ON_MESSAGE(WM_DPICHANGED, &CProcessConnectionsWindow::OnDpiChanged)
END_MESSAGE_MAP()

CProcessConnectionsWindow::~CProcessConnectionsWindow()
{
    if (::IsWindow(GetSafeHwnd()))
        DestroyWindow();
}

BOOL CProcessConnectionsWindow::ShowFor(CWnd* owner, DWORD pid, const CString& name)
{
    if (!::IsWindow(GetSafeHwnd()))
    {
        const CString windowClass = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW),
            nullptr, AfxGetApp()->LoadIcon(IDR_MAINFRAME));
        if (!CreateEx(WS_EX_APPWINDOW, windowClass, L"连接 IP 与归属地", WS_OVERLAPPEDWINDOW,
            CRect(0, 0, 940, 540), owner, 0))
            return FALSE;
        MONITORINFO monitor{ sizeof(monitor) };
        if (::GetMonitorInfo(::MonitorFromWindow(owner ? owner->GetSafeHwnd() : m_hWnd, MONITOR_DEFAULTTONEAREST), &monitor))
        {
            const int width = (std::min<int>)(Scale(940), monitor.rcWork.right - monitor.rcWork.left);
            const int height = (std::min<int>)(Scale(560), monitor.rcWork.bottom - monitor.rcWork.top);
            SetWindowPos(nullptr, monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - width) / 2,
                monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - height) / 2,
                width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    // 查询只处理本地有界数据；等待旧结果后切换，避免旧进程结果覆盖新窗口。
    if (m_work.valid())
        m_work.get();
    m_pid = pid;
    m_process.reset();
    m_identityChanged = false;
    CString currentName(name);
    if (pid != 0 && pid != 4)
    {
        HANDLE process = ::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (process != nullptr)
        {
            m_process = std::shared_ptr<void>(process, [](void* handle) { ::CloseHandle(handle); });
            wchar_t path[32768]{};
            DWORD length = _countof(path);
            if (::QueryFullProcessImageNameW(process, 0, path, &length))
            {
                const std::wstring fullPath(path, length);
                const size_t separator = fullPath.find_last_of(L"/\\");
                const CString actualName((separator == std::wstring::npos ? fullPath : fullPath.substr(separator + 1)).c_str());
                const bool fallbackName = name.Left(4).CompareNoCase(L"PID ") == 0
                    || name.Left(3) == L"程序 " || name == L"未知程序";
                m_identityChanged = !fallbackName && actualName.CompareNoCase(name) != 0;
                currentName = actualName;
            }
            else
                m_process.reset();
        }
    }
    CString title = L"IP 归属地查询";
    if (pid != 0)
        title.Format(L"连接 IP 与归属地 — %s（PID %lu）", currentName.GetString(), static_cast<unsigned long>(pid));
    SetWindowText(title);
    m_rows.clear();
    m_pendingQuery.clear();
    m_list.DeleteAllItems();
    m_status.SetWindowText(L"正在读取本机连接与离线数据库…");
    ShowWindow(SW_SHOWNORMAL);
    SetForegroundWindow();
    SetTimer(RefreshTimer, 200, nullptr);
    m_refreshRequested = true;
    StartWork();
    return TRUE;
}

int CProcessConnectionsWindow::OnCreate(LPCREATESTRUCT create)
{
    if (CWnd::OnCreate(create) == -1)
        return -1;
    const DWORD child = WS_CHILD | WS_VISIBLE;
    if (!m_hint.Create(L"当前 TCP 连接 · 每 2 秒刷新 · 归属地在本机离线查询", child, CRect(), this)
        || !m_input.Create(child | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, CRect(), this, 100)
        || !m_query.Create(L"查询 IP", child | WS_TABSTOP | BS_PUSHBUTTON, CRect(), this, QueryId)
        || !m_refresh.Create(L"刷新连接", child | WS_TABSTOP | BS_PUSHBUTTON, CRect(), this, RefreshId)
        || !m_lookup.Create(L"输入 IPv4 / IPv6 可单独查询归属地。", child, CRect(), this)
        || !m_list.Create(child | WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS, CRect(), this, 103)
        || !m_status.Create(L"", child, CRect(), this))
        return -1;
    m_input.SetLimitText(128);
    m_input.SendMessage(EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"输入 IPv4 或 IPv6 地址"));
    m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    m_list.InsertColumn(0, L"远端 IP : 端口", LVCFMT_LEFT, 250);
    m_list.InsertColumn(1, L"归属地 / 运营商", LVCFMT_LEFT, 300);
    m_list.InsertColumn(2, L"连接状态", LVCFMT_LEFT, 110);
    m_list.InsertColumn(3, L"本地 IP : 端口", LVCFMT_LEFT, 230);
    HDC dc = ::GetDC(m_hWnd);
    const int dpi = dc == nullptr ? 96 : ::GetDeviceCaps(dc, LOGPIXELSX);
    if (dc != nullptr)
        ::ReleaseDC(m_hWnd, dc);
    ApplyDpi(static_cast<UINT>(dpi > 0 ? dpi : 96));
    ApplyTheme();
    return 0;
}

int CProcessConnectionsWindow::Scale(int value) const
{
    return ::MulDiv(value, static_cast<int>(m_dpi), 96);
}

void CProcessConnectionsWindow::ApplyDpi(UINT dpi)
{
    m_dpi = (std::max)(72U, (std::min)(768U, dpi));
    m_font.DeleteObject();
    m_font.CreateFont(-Scale(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    for (CWnd* child = GetWindow(GW_CHILD); child != nullptr; child = child->GetNextWindow())
        child->SetFont(&m_font);
    Layout();
}

void CProcessConnectionsWindow::ApplyTheme()
{
    HIGHCONTRAST contrast{ sizeof(contrast) };
    const bool highContrast = ::SystemParametersInfo(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0)
        && (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
    DWORD lightTheme = 1, bytes = sizeof(lightTheme);
    ::RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &lightTheme, &bytes);
    const COLORREF background = highContrast ? ::GetSysColor(COLOR_WINDOW)
        : (lightTheme ? RGB(237, 242, 248) : RGB(30, 36, 46));
    m_foreground = highContrast ? ::GetSysColor(COLOR_WINDOWTEXT)
        : (lightTheme ? RGB(55, 73, 95) : RGB(229, 235, 244));
    m_background.DeleteObject();
    m_background.CreateSolidBrush(background);
    if (::IsWindow(m_list.GetSafeHwnd()))
    {
        m_list.SetBkColor(background);
        m_list.SetTextBkColor(background);
        m_list.SetTextColor(m_foreground);
        RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
    }
}

void CProcessConnectionsWindow::OnSettingChange(UINT flags, LPCTSTR section)
{
    CWnd::OnSettingChange(flags, section);
    ApplyTheme();
}

void CProcessConnectionsWindow::OnSysColorChange()
{
    CWnd::OnSysColorChange();
    ApplyTheme();
}

void CProcessConnectionsWindow::OnSize(UINT type, int width, int height)
{
    CWnd::OnSize(type, width, height);
    Layout();
}

void CProcessConnectionsWindow::Layout()
{
    if (!::IsWindow(m_list.GetSafeHwnd()))
        return;
    CRect rect;
    GetClientRect(&rect);
    const int padding = Scale(16);
    const int width = (std::max)(1, rect.Width() - padding * 2);
    m_hint.MoveWindow(padding, padding, width, Scale(25));
    const int inputTop = padding + Scale(33);
    const int buttonWidth = Scale(88);
    m_input.MoveWindow(padding, inputTop, (std::max)(Scale(60), width - buttonWidth * 2 - Scale(20)), Scale(30));
    m_query.MoveWindow(rect.right - padding - buttonWidth * 2 - Scale(10), inputTop, buttonWidth, Scale(30));
    m_refresh.MoveWindow(rect.right - padding - buttonWidth, inputTop, buttonWidth, Scale(30));
    m_lookup.MoveWindow(padding, inputTop + Scale(38), width, Scale(42));
    const int listTop = inputTop + Scale(86);
    const int listHeight = (std::max)(1, rect.Height() - listTop - padding - Scale(66));
    m_list.MoveWindow(padding, listTop, width, listHeight);
    const int statusTop = listTop + listHeight + Scale(10);
    m_status.MoveWindow(padding, statusTop, width, (std::max)(1, rect.Height() - statusTop - padding));
    // IPv6 列保留足够宽度，小窗口可横向滚动查看完整端点。
    m_list.SetColumnWidth(0, Scale(250));
    m_list.SetColumnWidth(1, (std::max)(Scale(240), width - Scale(600)));
    m_list.SetColumnWidth(2, Scale(110));
    m_list.SetColumnWidth(3, Scale(240));
}

void CProcessConnectionsWindow::OnGetMinMaxInfo(MINMAXINFO* info)
{
    CWnd::OnGetMinMaxInfo(info);
    info->ptMinTrackSize = CPoint(Scale(540), Scale(360));
    MONITORINFO monitor{ sizeof(monitor) };
    if (::GetMonitorInfo(::MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &monitor))
    {
        info->ptMinTrackSize.x = (std::min)(info->ptMinTrackSize.x, monitor.rcWork.right - monitor.rcWork.left);
        info->ptMinTrackSize.y = (std::min)(info->ptMinTrackSize.y, monitor.rcWork.bottom - monitor.rcWork.top);
    }
}

void CProcessConnectionsWindow::StartWork()
{
    if (m_work.valid() || !IsWindowVisible())
        return;
    m_refreshRequested = false;
    const DWORD pid = m_pid;
    const auto process = m_process;
    const bool identityChanged = m_identityChanged;
    const std::wstring query = std::move(m_pendingQuery);
    m_pendingQuery.clear();
    try
    {
    m_work = std::async(std::launch::async, [pid, process, query, identityChanged]()
    {
        Result result;
        try
        {
            auto endpoint = [](const std::wstring& address, unsigned short port)
            {
                return (address.find(L':') == std::wstring::npos ? address : L"[" + address + L"]")
                    + L":" + std::to_wstring(port);
            };
            if (!query.empty())
                result.lookup = query + L"  →  " + OfflineIpRegion::Lookup(query);
            const bool exited = process && ::WaitForSingleObject(process.get(), 0) != WAIT_TIMEOUT;
            if (pid == 0)
                result.status = L"点击排行榜里的程序可查看它当前的 TCP 连接。";
            else if (identityChanged)
                result.status = L"原程序记录已失效，PID 已被其他程序使用；请从排行榜重新打开。";
            else if (exited)
                result.status = L"程序已退出，当前连接已清空。";
            else if (pid != 4 && !process)
                result.status = L"无法确认程序身份；仍可在上方手动查询 IP。";
            else
            {
                const auto snapshot = ProcessConnections::Read(pid);
                if (snapshot.error != ERROR_SUCCESS && snapshot.connections.empty())
                    result.status = L"暂时无法读取当前连接，请稍后刷新。";
                else
                {
                    for (const auto& row : snapshot.connections)
                        result.rows.push_back({ endpoint(row.remoteAddress, row.remotePort),
                            OfflineIpRegion::Lookup(row.remoteAddress), row.state, endpoint(row.localAddress, row.localPort) });
                    result.status = result.rows.empty() ? L"当前没有可显示的 TCP 连接。"
                        : L"当前 TCP 连接 " + std::to_wstring(result.rows.size()) + L" 条。";
                    if (snapshot.truncated)
                        result.status += L"连接过多，仅显示前 2048 条。";
                    if (snapshot.error != ERROR_SUCCESS)
                        result.status += L"部分连接暂时无法读取。";
                    if (process && ::WaitForSingleObject(process.get(), 0) != WAIT_TIMEOUT)
                    {
                        result.rows.clear();
                        result.status = L"程序已退出，当前连接已清空。";
                    }
                }
            }
            result.status += L"  " + OfflineIpRegion::Status();
            result.status += L"\r\n地区为近似结果；内网、代理或 VPN 地址不代表网站实际位置。UDP 端点表不提供远端 IP。";
        }
        catch (...)
        {
            result.rows.clear();
            result.status = L"本次查询未完成，请稍后刷新。";
        }
        return result;
    });
    }
    catch (...)
    {
        m_status.SetWindowText(L"暂时无法启动查询，请稍后刷新。");
        m_nextRefresh = ::GetTickCount64() + 2000;
    }
}

void CProcessConnectionsWindow::ApplyResult(const Result& result)
{
    if (result.rows != m_rows)
    {
        std::wstring selected;
        const int selectedIndex = m_list.GetNextItem(-1, LVNI_SELECTED);
        if (selectedIndex >= 0 && static_cast<size_t>(selectedIndex) < m_rows.size())
            selected = m_rows[selectedIndex][0] + L"|" + m_rows[selectedIndex][3];
        const int top = m_list.GetTopIndex();
        m_list.SetRedraw(FALSE);
        m_list.DeleteAllItems();
        m_rows = result.rows;
        for (size_t i = 0; i < m_rows.size(); ++i)
        {
            m_list.InsertItem(static_cast<int>(i), m_rows[i][0].c_str());
            for (int column = 1; column < 4; ++column)
                m_list.SetItemText(static_cast<int>(i), column, m_rows[i][column].c_str());
            if (!selected.empty() && m_rows[i][0] + L"|" + m_rows[i][3] == selected)
                m_list.SetItemState(static_cast<int>(i), LVIS_SELECTED, LVIS_SELECTED);
        }
        if (!m_rows.empty())
            m_list.EnsureVisible((std::min)(top, static_cast<int>(m_rows.size() - 1)), FALSE);
        m_list.SetRedraw(TRUE);
        m_list.Invalidate(FALSE);
    }
    if (!result.lookup.empty())
        m_lookup.SetWindowText(result.lookup.c_str());
    m_status.SetWindowText(result.status.c_str());
}

void CProcessConnectionsWindow::OnTimer(UINT_PTR timer)
{
    if (timer != RefreshTimer)
    {
        CWnd::OnTimer(timer);
        return;
    }
    if (m_work.valid() && m_work.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
    {
        ApplyResult(m_work.get());
        m_nextRefresh = ::GetTickCount64() + 2000;
    }
    if (!m_work.valid() && (m_refreshRequested || ::GetTickCount64() >= m_nextRefresh))
        StartWork();
}

void CProcessConnectionsWindow::OnQuery()
{
    CString input;
    m_input.GetWindowText(input);
    input.Trim();
    if (input.IsEmpty())
    {
        m_lookup.SetWindowText(L"请先输入 IPv4 或 IPv6 地址。");
        m_input.SetFocus();
        return;
    }
    m_pendingQuery = input.GetString();
    m_refreshRequested = true;
    m_lookup.SetWindowText(L"正在离线查询…");
    StartWork();
}

void CProcessConnectionsWindow::OnRefresh()
{
    m_refreshRequested = true;
    StartWork();
}

BOOL CProcessConnectionsWindow::PreTranslateMessage(MSG* message)
{
    if (message->message == WM_KEYDOWN && message->wParam == VK_ESCAPE)
    {
        OnClose();
        return TRUE;
    }
    if (message->message == WM_KEYDOWN && message->wParam == VK_RETURN && GetFocus() == &m_input)
    {
        OnQuery();
        return TRUE;
    }
    if (::IsDialogMessage(m_hWnd, message))
        return TRUE;
    return CWnd::PreTranslateMessage(message);
}

void CProcessConnectionsWindow::OnClose()
{
    KillTimer(RefreshTimer);
    ShowWindow(SW_HIDE);
}

void CProcessConnectionsWindow::OnDestroy()
{
    KillTimer(RefreshTimer);
    if (m_work.valid())
        m_work.get();
    m_process.reset();
    CWnd::OnDestroy();
}

BOOL CProcessConnectionsWindow::OnEraseBkgnd(CDC* dc)
{
    CRect rect;
    GetClientRect(&rect);
    dc->FillRect(rect, &m_background);
    return TRUE;
}

HBRUSH CProcessConnectionsWindow::OnCtlColor(CDC* dc, CWnd* window, UINT type)
{
    if (type == CTLCOLOR_STATIC)
    {
        dc->SetBkMode(TRANSPARENT);
        dc->SetTextColor(m_foreground);
        return static_cast<HBRUSH>(m_background.GetSafeHandle());
    }
    return CWnd::OnCtlColor(dc, window, type);
}

LRESULT CProcessConnectionsWindow::OnDpiChanged(WPARAM dpi, LPARAM suggestedRect)
{
    ApplyDpi(LOWORD(dpi));
    const auto rect = reinterpret_cast<const RECT*>(suggestedRect);
    if (rect != nullptr)
        SetWindowPos(nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
    return 0;
}

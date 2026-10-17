#pragma once

#include <afxwin.h>
#include <cstdint>
#include <string>
#include <vector>

// TrafficMonitor 本地维护扩展；上游版权和许可证保留在仓库 LICENSE 中。
// 所有接口都由创建窗口的 UI 线程调用；采样线程通过主窗口转交快照。
class CProcessTrafficPopup : public CWnd
{
public:
    struct Row
    {
        std::wstring name;
        DWORD pid{};
        std::uint64_t uploadBytesPerSecond{};
        std::uint64_t downloadBytesPerSecond{};
        std::uint64_t totalBytes{};
    };

    struct Snapshot
    {
        std::vector<Row> rows;
        std::wstring status;
        std::wstring extraSummary;
        std::uint64_t uploadBytesPerSecond{};
        std::uint64_t downloadBytesPerSecond{};
        double cpuUsagePercent{ -1.0 };
        double memoryUsagePercent{ -1.0 };
        bool canEnable{ false };
    };

    static constexpr UINT EnableMessage = WM_APP + 180;
    static constexpr UINT ConnectionsMessage = WM_APP + 181;

    CProcessTrafficPopup() = default;
    virtual ~CProcessTrafficPopup();

    BOOL Create(CWnd* owner);
    void SetData(const Snapshot& snapshot);
    void ShowAt(CPoint screenPoint);
    void ShowAt(const CRect& trayIconRect);
    void RequestHide();
    void Hide();
    CString NameForPid(DWORD pid) const;

protected:
    DECLARE_MESSAGE_MAP()
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* dc);
    afx_msg int OnMouseActivate(CWnd* desktopWnd, UINT hitTest, UINT message);
    afx_msg void OnMouseMove(UINT flags, CPoint point);
    afx_msg void OnLButtonDown(UINT flags, CPoint point);
    afx_msg BOOL OnSetCursor(CWnd* window, UINT hitTest, UINT message);
    afx_msg void OnTimer(UINT_PTR timerId);
    afx_msg void OnDestroy();
    afx_msg void OnSettingChange(UINT flags, LPCTSTR section);
    afx_msg void OnSysColorChange();
    afx_msg LRESULT OnDpiChanged(WPARAM wParam, LPARAM lParam);

private:
    static constexpr UINT_PTR TrackingTimer = 1;
    static constexpr size_t MaxRows = 8;

    Snapshot m_data;
    HWND m_owner{};
    CRect m_anchorRect;
    CRect m_enableRect;
    CRect m_lookupRect;
    UINT m_dpi{ 96 };
    size_t m_visibleRows{};
    DWORD m_outsideSince{};
    bool m_outside{ false };
    bool m_enableHovered{ false };
    bool m_positioning{ false };
    bool m_darkTheme{ false };
    bool m_highContrast{ false };
    CFont m_bodyFont;
    CFont m_titleFont;
    CFont m_smallFont;

    int Scale(int pixels) const;
    void ApplyDpi(UINT dpi);
    void RefreshAppearance();
    void UpdateWindowRegion(int width, int height);
    void PositionWindow(bool show);
    void DrawContent(CDC& dc, const CRect& clientRect);
    void UpdateAccessibleText();
    static CString DisplayName(const Row& row);
    bool HasSystemTraffic() const;
    const Row* RowAt(CPoint point) const;
    bool IsPointerInside(CPoint screenPoint) const;
    static CString FormatBytes(std::uint64_t bytes, bool perSecond = false);
    static CString FormatUsage(double usage);
    static std::uint64_t TotalRate(const Row& row);
};

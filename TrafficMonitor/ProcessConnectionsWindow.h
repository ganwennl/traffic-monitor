#pragma once

#include <afxwin.h>
#include <afxcmn.h>
#include <future>
#include <memory>
#include <string>
#include <vector>

// 连接端点只保留在此窗口的内存中，不写配置或日志。
class CProcessConnectionsWindow : public CWnd
{
public:
    ~CProcessConnectionsWindow() override;
    BOOL ShowFor(CWnd* owner, DWORD pid, const CString& name);
    BOOL PreTranslateMessage(MSG* message) override;

protected:
    DECLARE_MESSAGE_MAP()
    afx_msg int OnCreate(LPCREATESTRUCT create);
    afx_msg void OnSize(UINT type, int width, int height);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO* info);
    afx_msg void OnClose();
    afx_msg void OnDestroy();
    afx_msg void OnTimer(UINT_PTR timer);
    afx_msg void OnQuery();
    afx_msg void OnRefresh();
    afx_msg void OnSettingChange(UINT flags, LPCTSTR section);
    afx_msg void OnSysColorChange();
    afx_msg HBRUSH OnCtlColor(CDC* dc, CWnd* window, UINT type);
    afx_msg BOOL OnEraseBkgnd(CDC* dc);
    afx_msg LRESULT OnDpiChanged(WPARAM dpi, LPARAM suggestedRect);

private:
    struct Result
    {
        std::vector<std::vector<std::wstring>> rows;
        std::wstring status;
        std::wstring lookup;
    };
    static constexpr UINT QueryId = 101;
    static constexpr UINT RefreshId = 102;
    static constexpr UINT_PTR RefreshTimer = 1;
    DWORD m_pid{};
    UINT m_dpi{ 96 };
    std::shared_ptr<void> m_process;
    std::future<Result> m_work;
    std::wstring m_pendingQuery;
    bool m_refreshRequested{};
    bool m_identityChanged{};
    ULONGLONG m_nextRefresh{};
    CFont m_font;
    CBrush m_background{ RGB(237, 242, 248) };
    COLORREF m_foreground{ RGB(55, 73, 95) };
    CStatic m_hint;
    CEdit m_input;
    CButton m_query;
    CButton m_refresh;
    CStatic m_lookup;
    CListCtrl m_list;
    CStatic m_status;
    std::vector<std::vector<std::wstring>> m_rows;

    int Scale(int value) const;
    void ApplyDpi(UINT dpi);
    void ApplyTheme();
    void Layout();
    void StartWork();
    void ApplyResult(const Result& result);
};

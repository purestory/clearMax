#pragma once
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>

// Forward declarations
class MainWindow; 

class TrayApp {
public:
    TrayApp(HINSTANCE hInstance, MainWindow* pMainWindow);
    ~TrayApp();

    void InitTrayIcon();
    void RemoveTrayIcon();
    
    // Handlers
    void ShowContextMenu(POINT pt);
    void HandleTrayMessage(LPARAM lParam);
    
    // Actions
    void RunQuickDelete();
    void ShowConfigDialog();

    // Expose hidden window so main loop can send messages if needed
    HWND GetHWND() const { return m_hHiddenWnd; }

private:
    HINSTANCE m_hInstance;
    MainWindow* m_pMainWindow;
    HWND m_hHiddenWnd;
    NOTIFYICONDATAW m_nid;
    
    // Helper to get self path
    std::wstring GetExePath();
    
    static LRESULT CALLBACK HiddenWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static INT_PTR CALLBACK QuickDeleteDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
};

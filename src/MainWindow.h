#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include "resource.h"
#include "RecoveryTypes.h"
#include "RegistryMgr.h"

class MainWindow {
public:
    MainWindow(HINSTANCE hInstance);
    ~MainWindow();

    bool Initialize();
    void Show();
    HWND GetHWND() const { return m_hWnd; }

    static INT_PTR CALLBACK ConfigDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    HINSTANCE m_hInstance;
    HWND m_hWnd;
    HWND m_hTabControl;

    // Tab Dialogs
    HWND m_hTabPrograms;
    HWND m_hTabShredder;
    HWND m_hTabBrowser;
    HWND m_hTabRecovery;
    
    // Tab Data
    std::vector<RecoverableFile> m_recoveredFiles;
    std::vector<ProgramInfo> m_programs;

    // Window Procedures
    static INT_PTR CALLBACK MainDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static INT_PTR CALLBACK ProgramsDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static INT_PTR CALLBACK ShredderDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static INT_PTR CALLBACK BrowserDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static INT_PTR CALLBACK RecoveryDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    // Instance Handlers
    INT_PTR HandleMainMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    INT_PTR HandleProgramsMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    INT_PTR HandleShredderMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    INT_PTR HandleBrowserMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    INT_PTR HandleRecoveryMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    // Helpers
    void InitTabs();
    void OnTabChanged();
    void ResizeTabs();
    
    // Programs Logic
    void PopulateProgramsList(HWND hList);
    void FilterProgramsList(HWND hList, const std::wstring& filter);
    void ShowProgramsContextMenu(HWND hWnd, POINT pt);
    static int CALLBACK ListViewCompareProc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort);
    int m_sortColumn = -1;
    bool m_sortAscending = true;
    
    // Recovery Logic
    void PopulateDrivesCombo(HWND hCombo);
    void PopulateRecoveryTree(HWND hTree);
    void FilterRecoveryTree(HWND hTree, const std::wstring& filter);
    void ShowRecoveryContextMenu(HWND hWnd, POINT pt);
};

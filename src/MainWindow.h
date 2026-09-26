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
#include "StartupMgr.h"
#include "SpaceAnalyzer.h"
#include <atomic>
#include <memory>

#define WM_UPDATE_PROG_DATE (WM_USER + 101)
#define WM_APP_SHRED_PATH (WM_APP + 101)
#define WM_APP_ANALYZE_PATH (WM_APP + 102)

class MainWindow {
public:
    MainWindow(HINSTANCE hInstance);
    ~MainWindow();

    bool Initialize();
    void Show();
    HWND GetHWND() const { return m_hWnd; }

    static INT_PTR CALLBACK ConfigDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    static bool IsAutoStartEnabled();
    static void ToggleAutoStart(bool enable);

    void SetPendingShredPath(const std::wstring& path) { m_pendingShredPath = path; }
    void SetPendingAnalyzePath(const std::wstring& path) { m_pendingAnalyzePath = path; }

private:
    std::wstring m_pendingShredPath;
    std::wstring m_pendingAnalyzePath;
    HINSTANCE m_hInstance;
    HWND m_hWnd;
    HWND m_hTabControl;

    // Tab Dialogs
    HWND m_hTabPrograms;
    HWND m_hTabShredder;
    HWND m_hTabBrowser;
    HWND m_hTabRecovery;
    HWND m_hTabSettings;
    HWND m_hTabStartup;
    HWND m_hTabAnalyzer;
    
    // Tab Data
    std::vector<RecoverableFile> m_recoveredFiles;
    std::vector<ProgramInfo> m_programs;
    std::vector<StartupItem> m_startupItems;
    HIMAGELIST m_hProgImageList;
    HIMAGELIST m_hStartupImageList;

    std::unique_ptr<SpaceAnalyzer> m_spaceAnalyzer;
    SpaceAnalyzer::SortMode m_analyzerSortMode = SpaceAnalyzer::SortMode::SizeDesc;
    DirNode* m_currentDirNode;
    std::atomic<bool> m_cancelAnalyzer;

    // Window Procedures
    static INT_PTR CALLBACK MainDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static INT_PTR CALLBACK ProgramsDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static INT_PTR CALLBACK ShredderDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static INT_PTR CALLBACK BrowserDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static INT_PTR CALLBACK RecoveryDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static INT_PTR CALLBACK SettingsDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static INT_PTR CALLBACK StartupDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static INT_PTR CALLBACK AnalyzerDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    // Instance Handlers
    INT_PTR HandleMainMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    INT_PTR HandleProgramsMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    INT_PTR HandleShredderMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    INT_PTR HandleBrowserMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    INT_PTR HandleRecoveryMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    INT_PTR HandleSettingsMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    INT_PTR HandleStartupMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    INT_PTR HandleAnalyzerMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    // Helpers
    void UpdateTreeViewNode(HWND hTree, HTREEITEM hItem);
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

    // Startup Logic
    void PopulateStartupList(HWND hList);
    void FilterStartupList(HWND hList, int filterType = 0);
    void ShowStartupContextMenu(HWND hWnd, POINT pt);
    static int CALLBACK ListViewStartupCompareProc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort);
    int m_startupSortColumn = -1;
    bool m_startupSortAscending = true;

    // Task control
    std::atomic<bool> m_cancelShred{false};
    std::atomic<bool> m_pauseShred{false};
    std::atomic<bool> m_cancelRecovery{false};
    std::atomic<bool> m_pauseRecovery{false};
    std::atomic<int> m_dateFetchEpoch{0};
};

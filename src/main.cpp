#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include "TrayApp.h"
#include "MainWindow.h"

// Enable Windows Visual Styles (Modern Theme)
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

bool isRunAsAdmin() {
    BOOL fIsRunAsAdmin = FALSE;
    DWORD dwError = ERROR_SUCCESS;
    PSID pAdministratorsGroup = NULL;

    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&NtAuthority, 2,
        SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0,
        &pAdministratorsGroup)) {
        
        CheckTokenMembership(NULL, pAdministratorsGroup, &fIsRunAsAdmin);
        FreeSid(pAdministratorsGroup);
    }
    return fIsRunAsAdmin == TRUE;
}

static void ForceForeground(HWND hWnd) {
    if (!hWnd) return;
    HWND hCurWnd = ::GetForegroundWindow();
    DWORD dwMyID = ::GetCurrentThreadId();
    DWORD dwCurID = hCurWnd ? ::GetWindowThreadProcessId(hCurWnd, NULL) : 0;
    
    if (dwCurID != 0 && dwCurID != dwMyID) {
        ::AttachThreadInput(dwCurID, dwMyID, TRUE);
        ::SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        ::SetWindowPos(hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        ::SetForegroundWindow(hWnd);
        ::BringWindowToTop(hWnd);
        ::AttachThreadInput(dwCurID, dwMyID, FALSE);
    } else {
        ::SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        ::SetWindowPos(hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        ::SetForegroundWindow(hWnd);
        ::BringWindowToTop(hWnd);
    }
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    // Prevent running in background sessions (e.g., from other users' Scheduled Tasks)
    DWORD dwConsoleSessionId = WTSGetActiveConsoleSessionId();
    DWORD dwCurrentSessionId = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &dwCurrentSessionId);
    if (dwConsoleSessionId != 0xFFFFFFFF && dwCurrentSessionId != dwConsoleSessionId) {
        return 0; // Exit silently
    }

    bool autoShred = false;
    std::wstring shredPath = L"";
    bool autoAnalyze = false;
    std::wstring analyzePath = L"";
    
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (_wcsicmp(argv[i], L"/shred") == 0 && i + 1 < argc) {
                autoShred = true;
                shredPath = argv[i + 1];
                break;
            } else if (_wcsicmp(argv[i], L"/analyze") == 0 && i + 1 < argc) {
                autoAnalyze = true;
                analyzePath = argv[i + 1];
                break;
            }
        }
        LocalFree(argv);
    }

    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"Local\\ClearMax_SingleInstanceMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hExisting = FindWindowW(L"#32770", L"clearMax");
        if (hExisting) {
            if (autoShred && !shredPath.empty()) {
                COPYDATASTRUCT cds;
                cds.dwData = 1;
                cds.cbData = (shredPath.length() + 1) * sizeof(wchar_t);
                cds.lpData = (void*)shredPath.c_str();
                SendMessageW(hExisting, WM_COPYDATA, 0, (LPARAM)&cds);
            } else if (autoAnalyze && !analyzePath.empty()) {
                COPYDATASTRUCT cds;
                cds.dwData = 2;
                cds.cbData = (analyzePath.length() + 1) * sizeof(wchar_t);
                cds.lpData = (void*)analyzePath.c_str();
                SendMessageW(hExisting, WM_COPYDATA, 0, (LPARAM)&cds);
            } else {
                ShowWindow(hExisting, SW_RESTORE);
                ForceForeground(hExisting);
            }
        }
        CloseHandle(hMutex);
        return 0;
    }

    if (!isRunAsAdmin()) {
        MessageBoxW(NULL, L"ClearMax requires Administrator privileges to access registry keys and perform secure file deletions on system drives.\n\nPlease restart the application as Administrator.", L"Administrator Privileges Required", MB_ICONERROR | MB_OK);
        return 1;
    }

    // Initialize Common Controls
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES | ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icex);

    MainWindow mainWin(hInstance);
    if (!mainWin.Initialize()) {
        MessageBoxW(NULL, L"Failed to create main window.", L"Error", MB_OK);
        return 1;
    }

    TrayApp tray(hInstance, &mainWin);
    tray.InitTrayIcon();

    // Check for autostart flag
    bool isAutoStart = false;
    if (lpCmdLine && wcsstr(lpCmdLine, L"/autostart") != NULL) {
        isAutoStart = true;
    }

    if (autoShred && !shredPath.empty()) {
        mainWin.SetPendingShredPath(shredPath);
        mainWin.Show();
    } else if (autoAnalyze && !analyzePath.empty()) {
        mainWin.SetPendingAnalyzePath(analyzePath);
        mainWin.Show();
    } else if (!isAutoStart) {
        mainWin.Show(); // Show window on normal startup
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!IsDialogMessage(mainWin.GetHWND(), &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int)msg.wParam;
}

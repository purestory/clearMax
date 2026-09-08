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

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"Global\\ClearMax_SingleInstanceMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Bring existing instance to front
        HWND hExisting = FindWindowW(L"#32770", L"clearMax"); // MainWindow is a Dialog (#32770)
        if (hExisting) {
            ShowWindow(hExisting, SW_RESTORE);
            SetForegroundWindow(hExisting);
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

    if (!isAutoStart) {
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

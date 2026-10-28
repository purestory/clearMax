#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <atomic>
#include <thread>
#include <string>
#include "TrayApp.h"
#include "MainWindow.h"
#include "RegistryMgr.h"
#include "FileShredder.h"
#include "resource.h"

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

#define WM_APP_SHRED_PROGRESS (WM_APP + 201)
#define WM_APP_SHRED_COMPLETE (WM_APP + 202)

struct StandaloneShredContext {
    std::wstring targetPath;
    std::atomic<bool> isRunning{false};
    std::atomic<bool> isCancelled{false};
    std::thread workerThread;
    bool isFinished{false};
};

static INT_PTR CALLBACK StandaloneShredDlgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    StandaloneShredContext* ctx = (StandaloneShredContext*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    switch (msg) {
        case WM_INITDIALOG: {
            ctx = (StandaloneShredContext*)lParam;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)ctx);

            HICON hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_APP_ICON));
            if (hIcon) {
                SendMessageW(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
                SendMessageW(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
            }

            SetDlgItemTextW(hWnd, IDC_LBL_STANDALONE_TARGET, ctx->targetPath.c_str());

            HWND hCmb = GetDlgItem(hWnd, IDC_CMB_STANDALONE_PASSES);
            SendMessageW(hCmb, CB_ADDSTRING, 0, (LPARAM)L"1회 (0x00 덮어쓰기 - 빠름)");
            SendMessageW(hCmb, CB_ADDSTRING, 0, (LPARAM)L"3회 (DoD 5220.22-M 미국 국방부 표준)");
            SendMessageW(hCmb, CB_ADDSTRING, 0, (LPARAM)L"7회 (Gutmann 알고리즘 - 최고 보안)");
            SendMessageW(hCmb, CB_SETCURSEL, 0, 0);

            SendDlgItemMessageW(hWnd, IDC_PROG_STANDALONE, PBM_SETRANGE32, 0, 100);
            SendDlgItemMessageW(hWnd, IDC_PROG_STANDALONE, PBM_SETPOS, 0, 0);
            SetDlgItemTextW(hWnd, IDC_LBL_STANDALONE_STATUS, L"대기 중 - [파쇄 시작]을 누르면 영구 삭제가 진행됩니다.");

            ForceForeground(hWnd);
            return (INT_PTR)TRUE;
        }

        case WM_APP_SHRED_PROGRESS: {
            int p = (int)wParam;
            SendDlgItemMessageW(hWnd, IDC_PROG_STANDALONE, PBM_SETPOS, p, 0);
            std::wstring status = L"파쇄 진행 중... (" + std::to_wstring(p) + L"%)";
            SetDlgItemTextW(hWnd, IDC_LBL_STANDALONE_STATUS, status.c_str());
            return (INT_PTR)TRUE;
        }

        case WM_APP_SHRED_COMPLETE: {
            if (ctx) {
                if (ctx->workerThread.joinable()) {
                    ctx->workerThread.join();
                }
                ctx->isRunning = false;
                ctx->isFinished = true;

                if (ctx->isCancelled) {
                    SetDlgItemTextW(hWnd, IDC_LBL_STANDALONE_STATUS, L"파쇄 작업이 사용자에 의해 중지되었습니다.");
                    MessageBoxW(hWnd, L"파쇄 작업이 중지되었습니다.", L"알림", MB_OK | MB_ICONINFORMATION);
                } else {
                    SendDlgItemMessageW(hWnd, IDC_PROG_STANDALONE, PBM_SETPOS, 100, 0);
                    SetDlgItemTextW(hWnd, IDC_LBL_STANDALONE_STATUS, L"파쇄 완료 - 대상이 영구히 삭제되었습니다.");
                    MessageBoxW(hWnd, L"선택한 항목이 완전히 파쇄되어 영구 삭제되었습니다.", L"파쇄 완료", MB_OK | MB_ICONINFORMATION);
                }

                SetDlgItemTextW(hWnd, IDCANCEL, L"닫기");
            }
            return (INT_PTR)TRUE;
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            if (wmId == IDC_BTN_STANDALONE_START) {
                if (!ctx || ctx->isRunning) return (INT_PTR)TRUE;

                std::wstring confirmMsg = L"정말로 다음 항목을 복구 불가능하게 영구 파쇄하시겠습니까?\n\n";
                confirmMsg += ctx->targetPath;
                confirmMsg += L"\n\n※ 파쇄된 데이터는 어떤 복구 프로그램으로도 되살릴 수 없습니다.";

                if (MessageBoxW(hWnd, confirmMsg.c_str(), L"경고 - 파일 영구 파쇄", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
                    return (INT_PTR)TRUE;
                }

                HWND hCmb = GetDlgItem(hWnd, IDC_CMB_STANDALONE_PASSES);
                int sel = (int)SendMessageW(hCmb, CB_GETCURSEL, 0, 0);
                ShredPass passes = ShredPass::Pass_1;
                if (sel == 1) passes = ShredPass::Pass_3;
                else if (sel == 2) passes = ShredPass::Pass_7;

                ctx->isRunning = true;
                ctx->isCancelled = false;
                ctx->isFinished = false;

                EnableWindow(GetDlgItem(hWnd, IDC_BTN_STANDALONE_START), FALSE);
                EnableWindow(GetDlgItem(hWnd, IDC_CMB_STANDALONE_PASSES), FALSE);
                SetDlgItemTextW(hWnd, IDCANCEL, L"중지");
                SetDlgItemTextW(hWnd, IDC_LBL_STANDALONE_STATUS, L"파쇄 준비 중...");

                ctx->workerThread = std::thread([hWnd, ctx, passes]() {
                    auto cancelCheck = [ctx]() -> bool {
                        return ctx->isCancelled.load();
                    };

                    FileShredder::shredPath(ctx->targetPath, passes, [hWnd](int p, const std::wstring&) {
                        PostMessageW(hWnd, WM_APP_SHRED_PROGRESS, (WPARAM)p, 0);
                    }, cancelCheck);

                    PostMessageW(hWnd, WM_APP_SHRED_COMPLETE, 0, 0);
                });
                return (INT_PTR)TRUE;
            } else if (wmId == IDCANCEL) {
                if (ctx && ctx->isRunning) {
                    if (MessageBoxW(hWnd, L"진행 중인 파쇄 작업을 중지하시겠습니까?", L"중지 확인", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        ctx->isCancelled = true;
                        SetDlgItemTextW(hWnd, IDC_LBL_STANDALONE_STATUS, L"중지 요청 중... 잠시만 기다려주세요.");
                    }
                    return (INT_PTR)TRUE;
                }

                if (ctx && ctx->workerThread.joinable()) {
                    ctx->workerThread.join();
                }
                EndDialog(hWnd, 0);
                return (INT_PTR)TRUE;
            }
            break;
        }

        case WM_CLOSE: {
            if (ctx && ctx->isRunning) {
                if (MessageBoxW(hWnd, L"파쇄 작업이 진행 중입니다. 작업을 중지하고 닫으시겠습니까?", L"경고", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    ctx->isCancelled = true;
                    SetDlgItemTextW(hWnd, IDC_LBL_STANDALONE_STATUS, L"중지 요청 중... 잠시만 기다려주세요.");
                    if (ctx->workerThread.joinable()) {
                        ctx->workerThread.join();
                    }
                    EndDialog(hWnd, 0);
                }
                return (INT_PTR)TRUE;
            }

            if (ctx && ctx->workerThread.joinable()) {
                ctx->workerThread.join();
            }
            EndDialog(hWnd, 0);
            return (INT_PTR)TRUE;
        }
    }
    return (INT_PTR)FALSE;
}

static int ShowStandaloneShredDialog(HINSTANCE hInstance, const std::wstring& path) {
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icex);

    StandaloneShredContext ctx;
    ctx.targetPath = path;

    DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_SHRED_STANDALONE_DIALOG), NULL, StandaloneShredDlgProc, (LPARAM)&ctx);
    return 0;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (_wcsicmp(argv[i], L"/safepurge_service") == 0) {
                LocalFree(argv);
                return RegistryMgr::RunSafePurgeService();
            }
        }
    }

    // Prevent running in background sessions (e.g., from other users' Scheduled Tasks)
    DWORD dwConsoleSessionId = WTSGetActiveConsoleSessionId();
    DWORD dwCurrentSessionId = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &dwCurrentSessionId);
    if (dwConsoleSessionId != 0xFFFFFFFF && dwCurrentSessionId != dwConsoleSessionId) {
        if (argv) LocalFree(argv);
        return 0; // Exit silently
    }

    bool autoShred = false;
    std::wstring shredPath = L"";
    bool autoAnalyze = false;
    std::wstring analyzePath = L"";
    
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

    // Early exit for standalone shredding: run lightweight dedicated dialog
    if (autoShred && !shredPath.empty()) {
        if (!isRunAsAdmin()) {
            MessageBoxW(NULL, L"clearMax는 시스템 파일 파쇄 및 안전한 삭제를 위해 관리자 권한이 필요합니다.\n\n관리자 권한으로 다시 실행해 주세요.", L"관리자 권한 필요", MB_ICONERROR | MB_OK);
            return 1;
        }
        return ShowStandaloneShredDialog(hInstance, shredPath);
    }

    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"Local\\ClearMax_SingleInstanceMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hExisting = FindWindowW(L"#32770", L"clearMax - 클리어맥스");
        if (!hExisting) {
            hExisting = FindWindowW(L"#32770", L"clearMax");
        }
        if (hExisting) {
            if (autoAnalyze && !analyzePath.empty()) {
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

    if (autoAnalyze && !analyzePath.empty()) {
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

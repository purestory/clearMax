#include "TrayApp.h"
#include "MainWindow.h"
#include "BrowserCleaner.h"
#include "FileShredder.h"
#include "resource.h"
#include <thread>
#include <commctrl.h>
#include <iostream>
#include <chrono>
#include <vector>

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_APP_ICON 1001

#define IDM_SHOW_UI 2001
#define IDM_QUICK_DELETE 2002
#define IDM_AUTO_START 2003
#define IDM_SETTINGS 2004
#define IDM_EXIT 2005

LRESULT CALLBACK TrayApp::HiddenWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CREATE) {
        LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
        TrayApp* tray = reinterpret_cast<TrayApp*>(pCreateStruct->lpCreateParams);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)tray);
        return 0;
    } else if (message == WM_TRAYICON) {
        TrayApp* tray = (TrayApp*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        if (tray) {
            tray->HandleTrayMessage(lParam);
        }
    } else if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

TrayApp::TrayApp(HINSTANCE hInstance, MainWindow* pMainWindow) 
    : m_hInstance(hInstance), m_pMainWindow(pMainWindow), m_hHiddenWnd(NULL) {
    ZeroMemory(&m_nid, sizeof(m_nid));

    WNDCLASSEXW wcex = {0};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.lpfnWndProc = HiddenWndProc;
    wcex.hInstance = hInstance;
    wcex.lpszClassName = L"ClearMaxTrayClass";
    RegisterClassExW(&wcex);

    m_hHiddenWnd = CreateWindowExW(0, L"ClearMaxTrayClass", L"ClearMaxTray", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, this);
}

TrayApp::~TrayApp() {
    RemoveTrayIcon();
    if (m_hHiddenWnd) {
        DestroyWindow(m_hHiddenWnd);
    }
}

void TrayApp::InitTrayIcon() {
    m_nid.cbSize = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd = m_hHiddenWnd;
    m_nid.uID = ID_TRAY_APP_ICON;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAYICON;
    m_nid.hIcon = LoadIcon(m_hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
    if (!m_nid.hIcon) m_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    lstrcpyW(m_nid.szTip, L"clearMax");

    Shell_NotifyIconW(NIM_ADD, &m_nid);
}

void TrayApp::RemoveTrayIcon() {
    Shell_NotifyIconW(NIM_DELETE, &m_nid);
}

void TrayApp::HandleTrayMessage(LPARAM lParam) {
    if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
        POINT pt;
        GetCursorPos(&pt);
        ShowContextMenu(pt);
    } else if (lParam == WM_LBUTTONDBLCLK) {
        if (m_pMainWindow) m_pMainWindow->Show();
    }
}

void TrayApp::ShowContextMenu(POINT pt) {
    HMENU hMenu = CreatePopupMenu();
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_SHOW_UI, L"메인 화면 열기");
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_QUICK_DELETE, L"일괄 삭제 (Quick Delete)");
    
    UINT autoStartFlags = MF_BYPOSITION | MF_STRING;
    if (IsAutoStartEnabled()) autoStartFlags |= MF_CHECKED;
    InsertMenuW(hMenu, -1, autoStartFlags, IDM_AUTO_START, L"자동 시작");
    
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_SETTINGS, L"설정...");
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_EXIT, L"종료");

    SetForegroundWindow(m_hHiddenWnd); 
    
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, m_hHiddenWnd, NULL);
    DestroyMenu(hMenu);

    if (cmd == IDM_SHOW_UI) {
        if (m_pMainWindow) m_pMainWindow->Show();
    } else if (cmd == IDM_QUICK_DELETE) {
        RunQuickDelete();
    } else if (cmd == IDM_AUTO_START) {
        ToggleAutoStart();
    } else if (cmd == IDM_SETTINGS) {
        ShowConfigDialog();
    } else if (cmd == IDM_EXIT) {
        PostMessage(m_hHiddenWnd, WM_CLOSE, 0, 0);
    }
}

struct QuickDeleteContext {
    TrayApp* pThis;
    HWND hDlg;
};

INT_PTR CALLBACK TrayApp::QuickDeleteDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INITDIALOG) {
        QuickDeleteContext* ctx = (QuickDeleteContext*)lParam;
        ctx->hDlg = hWnd;
        
        SendDlgItemMessage(hWnd, IDC_PROG_QUICK, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        SendDlgItemMessage(hWnd, IDC_PROG_QUICK, PBM_SETPOS, 0, 0);
        
        std::thread([ctx]() {
            bool doHistory = true;
            bool doCache = true;
            bool doCookies = false;
            bool doDownloads = false;
            bool doAllDrives = true;
            bool doMftOnly = true;
            
            HKEY hKey;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\clearMax", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                DWORD val;
                DWORD size = sizeof(DWORD);
                
                auto readConfig = [&](const wchar_t* name, bool& out) {
                    val = 0; size = sizeof(DWORD);
                    if (RegQueryValueExW(hKey, name, NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS)
                        out = (val != 0);
                };
                
                readConfig(L"CfgHistory", doHistory);
                readConfig(L"CfgCache", doCache);
                readConfig(L"CfgCookies", doCookies);
                readConfig(L"CfgDownloads", doDownloads);
                readConfig(L"CfgAllDrives", doAllDrives);
                readConfig(L"CfgMftOnly", doMftOnly);
                
                RegCloseKey(hKey);
            }
        
            std::vector<BrowserDataType> types;
            if (doHistory) types.push_back(BrowserDataType::History);
            if (doCache) types.push_back(BrowserDataType::Cache);
            if (doCookies) types.push_back(BrowserDataType::Cookies);
            if (doDownloads) types.push_back(BrowserDataType::Downloads);
            
            if (!types.empty()) {
                SetDlgItemTextW(ctx->hDlg, IDC_LBL_QUICK_STATUS, L"브라우저 데이터 정리 중...");
                BrowserCleaner::cleanBrowserData(BrowserType::Chrome, types, ShredPass::Pass_1, nullptr);
                BrowserCleaner::cleanBrowserData(BrowserType::Edge, types, ShredPass::Pass_1, nullptr);
                BrowserCleaner::cleanBrowserData(BrowserType::Firefox, types, ShredPass::Pass_1, nullptr);
            }

            WipeMode wipeMode = doMftOnly ? WipeMode::MftOnly : WipeMode::FullWipe;
            
            auto startTime = std::chrono::steady_clock::now();
            std::vector<std::wstring> drivesToWipe;
            
            if (doAllDrives) {
                DWORD drives = GetLogicalDrives();
                for (int i = 0; i < 26; ++i) {
                    if (drives & (1 << i)) {
                        std::wstring drv = std::wstring(1, (wchar_t)('A' + i)) + L":\\";
                        drivesToWipe.push_back(drv);
                    }
                }
            } else {
                drivesToWipe.push_back(L"C:\\");
            }
            
            int totalDrives = static_cast<int>(drivesToWipe.size());
            if (totalDrives > 0) {
                int currentDriveIndex = 0;
                for (const auto& drv : drivesToWipe) {
                    auto drvProgressCb = [ctx, drv, currentDriveIndex, totalDrives, startTime](int p) {
                        int overallProgress = (currentDriveIndex * 100 + p) / totalDrives;
                        
                        auto now = std::chrono::steady_clock::now();
                        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();
                        
                        std::wstring etaStr = L"계산 중...";
                        if (overallProgress > 0) {
                            int totalExpected = static_cast<int>((elapsed * 100) / overallProgress);
                            int remaining = totalExpected - static_cast<int>(elapsed);
                            if (remaining < 0) remaining = 0;
                            
                            int mins = remaining / 60;
                            int secs = remaining % 60;
                            etaStr = L"약 " + std::to_wstring(mins) + L"분 " + std::to_wstring(secs) + L"초";
                        }
                        
                        wchar_t buf[256];
                        swprintf_s(buf, L"[%s] 빈 공간 삭제 중...\n전체 진행률: %d%% (남은 시간: %s)", drv.c_str(), overallProgress, etaStr.c_str());
                        
                        SendDlgItemMessage(ctx->hDlg, IDC_PROG_QUICK, PBM_SETPOS, overallProgress, 0);
                        SetDlgItemTextW(ctx->hDlg, IDC_LBL_QUICK_STATUS, buf);
                    };
                    FileShredder::wipeFreeSpace(drv, wipeMode, drvProgressCb, nullptr);
                    currentDriveIndex++;
                }
            }
            
            typedef int(WINAPI* MSGBOXTIMEOUTW)(HWND, LPCWSTR, LPCWSTR, UINT, WORD, DWORD);
            HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
            if (hUser32) {
                MSGBOXTIMEOUTW msgBoxTimeout = (MSGBOXTIMEOUTW)GetProcAddress(hUser32, "MessageBoxTimeoutW");
                if (msgBoxTimeout) {
                    msgBoxTimeout(ctx->hDlg, L"일괄 삭제(Quick Delete) 작업이 완료되었습니다!\n(5초 후 자동 종료됩니다.)", L"clearMax", MB_OK | MB_ICONINFORMATION, 0, 5000);
                } else {
                    MessageBoxW(ctx->hDlg, L"일괄 삭제(Quick Delete) 작업이 완료되었습니다!", L"clearMax", MB_OK | MB_ICONINFORMATION);
                }
            } else {
                MessageBoxW(ctx->hDlg, L"일괄 삭제(Quick Delete) 작업이 완료되었습니다!", L"clearMax", MB_OK | MB_ICONINFORMATION);
            }
            PostMessage(ctx->hDlg, WM_CLOSE, 0, 0);
        }).detach();
        
        return (INT_PTR)TRUE;
    } else if (message == WM_COMMAND && LOWORD(wParam) == IDCANCEL) {
        EndDialog(hWnd, IDCANCEL);
        return (INT_PTR)TRUE;
    } else if (message == WM_CLOSE) {
        EndDialog(hWnd, IDOK);
        return (INT_PTR)TRUE;
    }
    return (INT_PTR)FALSE;
}

void TrayApp::RunQuickDelete() {
    QuickDeleteContext ctx = { this, NULL };
    DialogBoxParamW(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_QUICK_PROGRESS_DIALOG), m_hHiddenWnd, QuickDeleteDlgProc, (LPARAM)&ctx);
}

std::wstring TrayApp::GetExePath() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    return path;
}

bool TrayApp::IsAutoStartEnabled() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD type;
        if (RegQueryValueExW(hKey, L"clearMax", NULL, &type, NULL, NULL) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return true;
        }
        RegCloseKey(hKey);
    }
    return false;
}

void TrayApp::ToggleAutoStart() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        if (IsAutoStartEnabled()) {
            RegDeleteValueW(hKey, L"clearMax");
        } else {
            std::wstring path = L"\"" + GetExePath() + L"\" /autostart";
            RegSetValueExW(hKey, L"clearMax", 0, REG_SZ, (const BYTE*)path.c_str(), (path.length() + 1) * sizeof(wchar_t));
        }
        RegCloseKey(hKey);
    }
}

void TrayApp::ShowConfigDialog() {
    // Basic settings dialog using resource template
    DialogBoxW(m_hInstance, MAKEINTRESOURCEW(IDD_CONFIG_DIALOG), NULL, MainWindow::ConfigDlgProc);
}

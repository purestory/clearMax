#include "MainWindow.h"
#include "FileShredder.h"
#include "BrowserCleaner.h"
#include "NtfsRecovery.h"
#include "FatRecovery.h"
#include "RegistryMgr.h"
#include <windowsx.h>
#include <shobjidl.h>
#include <thread>
#include <shlobj.h>
#include <algorithm>

MainWindow::MainWindow(HINSTANCE hInstance) 
    : m_hInstance(hInstance), m_hWnd(NULL), m_hTabControl(NULL),
      m_hTabPrograms(NULL), m_hTabShredder(NULL), m_hTabBrowser(NULL), m_hTabRecovery(NULL) {
}

MainWindow::~MainWindow() {
}

bool MainWindow::Initialize() {
    m_hWnd = CreateDialogParamW(m_hInstance, MAKEINTRESOURCEW(IDD_MAIN_DIALOG), NULL, MainDlgProc, (LPARAM)this);
    if (!m_hWnd) return false;
    return true;
}

void MainWindow::Show() {
    if (m_hWnd) {
        ShowWindow(m_hWnd, SW_SHOW);
        UpdateWindow(m_hWnd);
    }
}

INT_PTR CALLBACK MainWindow::MainDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    MainWindow* pThis = nullptr;
    if (message == WM_INITDIALOG) {
        pThis = (MainWindow*)lParam;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
        pThis->m_hWnd = hWnd;
    } else {
        pThis = (MainWindow*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    }

    if (pThis) {
        return pThis->HandleMainMessage(hWnd, message, wParam, lParam);
    }
    return FALSE;
}

INT_PTR MainWindow::HandleMainMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_INITDIALOG:
            // Set the window icon (for Taskbar and Alt+Tab)
            SendMessageW(hWnd, WM_SETICON, ICON_BIG, (LPARAM)LoadIconW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDI_APP_ICON)));
            SendMessageW(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)LoadIconW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDI_APP_ICON)));
            InitTabs();
            return (INT_PTR)TRUE;
            
        case WM_NOTIFY: {
            LPNMHDR lpnmhdr = (LPNMHDR)lParam;
            if (lpnmhdr->code == TCN_SELCHANGE && lpnmhdr->idFrom == IDC_TAB_MAIN) {
                OnTabChanged();
            }
            break;
        }

        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_MINIMIZE) {
                ShowWindow(hWnd, SW_HIDE);
                return (INT_PTR)TRUE;
            } else if ((wParam & 0xFFF0) == SC_CLOSE) {
                ShowWindow(hWnd, SW_HIDE);
                return (INT_PTR)TRUE; // Just hide, don't close. Tray icon handles exit.
            }
            break;
            
        case WM_CLOSE:
            ShowWindow(hWnd, SW_HIDE);
            return (INT_PTR)TRUE;

        case WM_DESTROY:
            PostQuitMessage(0);
            return (INT_PTR)TRUE;
    }
    return (INT_PTR)FALSE;
}

void MainWindow::InitTabs() {
    m_hTabControl = GetDlgItem(m_hWnd, IDC_TAB_MAIN);

    TCITEMW tie;
    tie.mask = TCIF_TEXT | TCIF_IMAGE;
    tie.iImage = -1;
    
    tie.pszText = (LPWSTR)L"프로그램 관리";
    TabCtrl_InsertItem(m_hTabControl, 0, &tie);
    
    tie.pszText = (LPWSTR)L"파일 파쇄기";
    TabCtrl_InsertItem(m_hTabControl, 1, &tie);
    
    tie.pszText = (LPWSTR)L"브라우저 클리너";
    TabCtrl_InsertItem(m_hTabControl, 2, &tie);
    
    tie.pszText = (LPWSTR)L"파일 복구";
    TabCtrl_InsertItem(m_hTabControl, 3, &tie);

    m_hTabPrograms = CreateDialogParamW(m_hInstance, MAKEINTRESOURCEW(IDD_TAB_PROGRAMS), m_hTabControl, ProgramsDlgProc, (LPARAM)this);
    m_hTabShredder = CreateDialogParamW(m_hInstance, MAKEINTRESOURCEW(IDD_TAB_SHREDDER), m_hTabControl, ShredderDlgProc, (LPARAM)this);
    m_hTabBrowser  = CreateDialogParamW(m_hInstance, MAKEINTRESOURCEW(IDD_TAB_BROWSER), m_hTabControl, BrowserDlgProc, (LPARAM)this);
    m_hTabRecovery = CreateDialogParamW(m_hInstance, MAKEINTRESOURCEW(IDD_TAB_RECOVERY), m_hTabControl, RecoveryDlgProc, (LPARAM)this);

    ResizeTabs();
    OnTabChanged();
}

void MainWindow::ResizeTabs() {
    RECT rcClient, rcTab;
    GetClientRect(m_hTabControl, &rcClient);
    TabCtrl_AdjustRect(m_hTabControl, FALSE, &rcClient);

    HWND tabs[] = { m_hTabPrograms, m_hTabShredder, m_hTabBrowser, m_hTabRecovery };
    for (int i = 0; i < 4; ++i) {
        SetWindowPos(tabs[i], NULL, rcClient.left, rcClient.top, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top, SWP_NOZORDER);
    }
}

void MainWindow::OnTabChanged() {
    int sel = TabCtrl_GetCurSel(m_hTabControl);
    HWND tabs[] = { m_hTabPrograms, m_hTabShredder, m_hTabBrowser, m_hTabRecovery };
    
    for (int i = 0; i < 4; ++i) {
        ShowWindow(tabs[i], (i == sel) ? SW_SHOW : SW_HIDE);
    }
}

// -----------------------------------------------------------------------------
// Programs Tab
// -----------------------------------------------------------------------------
INT_PTR CALLBACK MainWindow::ProgramsDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    MainWindow* pThis = nullptr;
    if (message == WM_INITDIALOG) {
        pThis = (MainWindow*)lParam;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
        
        HWND hList = GetDlgItem(hWnd, IDC_LIST_PROGRAMS);
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        
        LVCOLUMNW lvc;
        lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
        lvc.fmt = LVCFMT_LEFT;
        
        const wchar_t* colNames[] = { L"Name", L"Publisher", L"Version", L"Size", L"Install Date", L"Ghost?" };
        int colWidths[] = { 200, 120, 80, 80, 90, 50 };
        
        for (int i = 0; i < 6; ++i) {
            lvc.iSubItem = i;
            lvc.cx = colWidths[i];
            lvc.pszText = (LPWSTR)colNames[i];
            lvc.fmt = (i == 3) ? LVCFMT_RIGHT : LVCFMT_LEFT;
            ListView_InsertColumn(hList, i, &lvc);
        }
        
        pThis->PopulateProgramsList(hList);
        
        return (INT_PTR)TRUE;
    } else {
        pThis = (MainWindow*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    }

    if (pThis) return pThis->HandleProgramsMessage(hWnd, message, wParam, lParam);
    return (INT_PTR)FALSE;
}

INT_PTR MainWindow::HandleProgramsMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_COMMAND) {
        int wmId = LOWORD(wParam);
        if (wmId == IDC_BTN_UNINSTALL || wmId == IDC_BTN_FORCE_REMOVE) {
            HWND hList = GetDlgItem(hWnd, IDC_LIST_PROGRAMS);
            int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (sel != -1 && sel < (int)m_programs.size()) {
                // Get the real index from item data because of filtering/sorting
                LVITEMW lvi;
                lvi.mask = LVIF_PARAM;
                lvi.iItem = sel;
                lvi.iSubItem = 0;
                ListView_GetItem(hList, &lvi);
                int realIndex = (int)lvi.lParam;

                if (realIndex >= 0 && realIndex < (int)m_programs.size()) {
                    const ProgramInfo& info = m_programs[realIndex];
                    if (wmId == IDC_BTN_UNINSTALL) {
                        RegistryMgr::uninstallProgram(info, nullptr);
                    } else if (wmId == IDC_BTN_FORCE_REMOVE) {
                        if (MessageBoxW(hWnd, L"정말로 이 항목을 레지스트리에서 강제로 삭제하시겠습니까?", L"경고", MB_YESNO | MB_ICONWARNING) == IDYES) {
                            if (RegistryMgr::forceRemoveProgram(info)) {
                                MessageBoxW(hWnd, L"삭제되었습니다.", L"알림", MB_OK);
                                PopulateProgramsList(hList);
                            } else {
                                MessageBoxW(hWnd, L"삭제 실패.", L"오류", MB_OK | MB_ICONERROR);
                            }
                        }
                    }
                }
            } else {
                MessageBoxW(hWnd, L"프로그램을 선택해주세요.", L"알림", MB_OK);
            }
        } else if (wmId == IDC_BTN_PROG_REFRESH) {
            PopulateProgramsList(GetDlgItem(hWnd, IDC_LIST_PROGRAMS));
            SetDlgItemTextW(hWnd, IDC_EDIT_PROG_SEARCH, L"");
        } else if (HIWORD(wParam) == EN_CHANGE && wmId == IDC_EDIT_PROG_SEARCH) {
            wchar_t searchBuf[256];
            GetDlgItemTextW(hWnd, IDC_EDIT_PROG_SEARCH, searchBuf, 256);
            FilterProgramsList(GetDlgItem(hWnd, IDC_LIST_PROGRAMS), searchBuf);
        }
    } else if (message == WM_NOTIFY) {
        LPNMHDR lpnmh = (LPNMHDR)lParam;
        if (lpnmh->idFrom == IDC_LIST_PROGRAMS) {
            if (lpnmh->code == NM_RCLICK) {
                POINT pt;
                GetCursorPos(&pt);
                ShowProgramsContextMenu(hWnd, pt);
            } else if (lpnmh->code == LVN_COLUMNCLICK) {
                LPNMLISTVIEW pnmv = (LPNMLISTVIEW)lParam;
                if (m_sortColumn == pnmv->iSubItem) {
                    m_sortAscending = !m_sortAscending;
                } else {
                    m_sortColumn = pnmv->iSubItem;
                    m_sortAscending = true;
                }
                ListView_SortItems(lpnmh->hwndFrom, MainWindow::ListViewCompareProc, (LPARAM)this);
            }
        }
    }
    return (INT_PTR)FALSE;
}

int CALLBACK MainWindow::ListViewCompareProc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort) {
    MainWindow* pThis = (MainWindow*)lParamSort;
    if (lParam1 < 0 || lParam1 >= pThis->m_programs.size() || 
        lParam2 < 0 || lParam2 >= pThis->m_programs.size()) return 0;
        
    const ProgramInfo& a = pThis->m_programs[lParam1];
    const ProgramInfo& b = pThis->m_programs[lParam2];
    
    int result = 0;
    switch (pThis->m_sortColumn) {
        case 0: result = a.displayName.compare(b.displayName); break;
        case 1: result = a.publisher.compare(b.publisher); break;
        case 2: result = a.displayVersion.compare(b.displayVersion); break;
        case 3: result = (a.estimatedSize > b.estimatedSize) ? 1 : (a.estimatedSize < b.estimatedSize ? -1 : 0); break;
        case 4: result = a.installDate.compare(b.installDate); break;
        case 5: result = (a.isGhost == b.isGhost) ? 0 : (a.isGhost ? 1 : -1); break;
    }
    
    return pThis->m_sortAscending ? result : -result;
}

void MainWindow::PopulateProgramsList(HWND hList) {
    ListView_DeleteAllItems(hList);
    m_programs = RegistryMgr::getInstalledPrograms();
    FilterProgramsList(hList, L"");
}

void MainWindow::FilterProgramsList(HWND hList, const std::wstring& filter) {
    ListView_DeleteAllItems(hList);
    
    std::wstring lowerFilter = filter;
    std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::towlower);
    
    int row = 0;
    for (size_t i = 0; i < m_programs.size(); ++i) {
        const auto& prog = m_programs[i];
        
        std::wstring nameLower = prog.displayName;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::towlower);
        
        if (!lowerFilter.empty() && nameLower.find(lowerFilter) == std::wstring::npos) {
            continue; // Skip if filter doesn't match
        }
        
        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem = row;
        lvi.iSubItem = 0;
        lvi.pszText = (LPWSTR)prog.displayName.c_str();
        lvi.lParam = static_cast<LPARAM>(i); // Store original index
        ListView_InsertItem(hList, &lvi);
        
        ListView_SetItemText(hList, row, 1, (LPWSTR)prog.publisher.c_str());
        ListView_SetItemText(hList, row, 2, (LPWSTR)prog.displayVersion.c_str());
        
        auto formatWithCommas = [](DWORD value) -> std::wstring {
            std::wstring s = std::to_wstring(value);
            int n = (int)s.length() - 3;
            while (n > 0) {
                s.insert(n, L",");
                n -= 3;
            }
            return s;
        };

        std::wstring sizeStr = L"";
        if (prog.estimatedSize > 0) {
            sizeStr = formatWithCommas(prog.estimatedSize) + L" KB";
        }
        
        std::wstring dateStr = prog.installDate;
        if (dateStr.length() == 8) {
            dateStr = dateStr.substr(0, 4) + L"-" + dateStr.substr(4, 2) + L"-" + dateStr.substr(6, 2);
        }
        
        ListView_SetItemText(hList, row, 3, (LPWSTR)sizeStr.c_str());
        ListView_SetItemText(hList, row, 4, (LPWSTR)dateStr.c_str());
        ListView_SetItemText(hList, row, 5, (LPWSTR)(prog.isGhost ? L"Yes" : L"No"));
        
        row++;
    }
}

void MainWindow::ShowProgramsContextMenu(HWND hWnd, POINT pt) {
    HWND hList = GetDlgItem(hWnd, IDC_LIST_PROGRAMS);
    int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
    if (sel == -1) return;
    
    HMENU hMenu = CreatePopupMenu();
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, IDC_BTN_UNINSTALL, L"Uninstall");
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, IDC_BTN_FORCE_REMOVE, L"Force Remove (Ghost)");
    
    int ret = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(hMenu);
    
    if (ret != 0) {
        SendMessageW(hWnd, WM_COMMAND, MAKEWPARAM(ret, 0), 0);
    }
}

// -----------------------------------------------------------------------------
// Shredder Tab
// -----------------------------------------------------------------------------
INT_PTR CALLBACK MainWindow::ShredderDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    MainWindow* pThis = nullptr;
    if (message == WM_INITDIALOG) {
        pThis = (MainWindow*)lParam;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
        
        // Init Combobox
        HWND hCombo = GetDlgItem(hWnd, IDC_CMB_SHRED_PASSES);
        ComboBox_AddString(hCombo, L"Fast (0 Passes)");
        ComboBox_AddString(hCombo, L"Standard (1 Pass)");
        ComboBox_AddString(hCombo, L"DoD 5220.22-M (3 Passes)");
        ComboBox_AddString(hCombo, L"Gutmann (7 Passes)");
        ComboBox_SetCurSel(hCombo, 1); // Default to 1 pass
        
        // Init Wipe Drive Combobox
        pThis->PopulateDrivesCombo(GetDlgItem(hWnd, IDC_CMB_WIPE_DRIVE));
        CheckDlgButton(hWnd, IDC_CHK_MFT_ONLY, BST_CHECKED);
        
        return (INT_PTR)TRUE;
    } else {
        pThis = (MainWindow*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    }

    if (pThis) return pThis->HandleShredderMessage(hWnd, message, wParam, lParam);
    return (INT_PTR)FALSE;
}

INT_PTR MainWindow::HandleShredderMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_COMMAND) {
        int wmId = LOWORD(wParam);
        
        if (wmId == IDC_BTN_SHRED_FILE || wmId == IDC_BTN_SHRED_FOLDER) {
            IFileOpenDialog* pFileOpen;
            if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen)))) {
                if (wmId == IDC_BTN_SHRED_FOLDER) {
                    DWORD dwOptions;
                    pFileOpen->GetOptions(&dwOptions);
                    pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS);
                }
                
                if (SUCCEEDED(pFileOpen->Show(hWnd))) {
                    IShellItem* pItem;
                    if (SUCCEEDED(pFileOpen->GetResult(&pItem))) {
                        PWSTR pszFilePath;
                        if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath))) {
                            std::wstring path = pszFilePath;
                            CoTaskMemFree(pszFilePath);
                            
                            int sel = ComboBox_GetCurSel(GetDlgItem(hWnd, IDC_CMB_SHRED_PASSES));
                            ShredPass passes = ShredPass::Pass_1;
                            if (sel == 0) passes = ShredPass::Pass_0;
                            else if (sel == 2) passes = ShredPass::Pass_3;
                            else if (sel == 3) passes = ShredPass::Pass_7;

                            std::wstring confirmMsg = L"정말로 다음 항목을 복구 불가능하게 파쇄하시겠습니까?\n" + path;
                            if (MessageBoxW(hWnd, confirmMsg.c_str(), L"경고", MB_YESNO | MB_ICONWARNING) == IDYES) {
                                std::thread([hWnd, path, passes, wmId]() {
                                    FileShredder::shredPath(path, passes, [hWnd](int p, const std::wstring&) {
                                        SendDlgItemMessage(hWnd, IDC_PROG_SHRED, PBM_SETPOS, p, 0);
                                    });
                                    MessageBoxW(hWnd, L"파쇄 완료.", L"알림", MB_OK);
                                    SendDlgItemMessage(hWnd, IDC_PROG_SHRED, PBM_SETPOS, 0, 0);
                                }).detach();
                            }
                        }
                        pItem->Release();
                    }
                }
                pFileOpen->Release();
            }
        } else if (wmId == IDC_BTN_WIPE_FREESPACE) {
            HWND hCombo = GetDlgItem(hWnd, IDC_CMB_WIPE_DRIVE);
            int drvSel = ComboBox_GetCurSel(hCombo);
            wchar_t driveStr[128];
            ComboBox_GetLBText(hCombo, drvSel, driveStr);
            
            bool mftOnly = (IsDlgButtonChecked(hWnd, IDC_CHK_MFT_ONLY) == BST_CHECKED);
            WipeMode mode = mftOnly ? WipeMode::MftOnly : WipeMode::FullWipe;
            
            std::wstring driveW(driveStr);
            std::thread([hWnd, driveW, mode]() {
                if (driveW.find(L"All Drives") != std::wstring::npos) {
                    DWORD drives = GetLogicalDrives();
                    for (int i = 0; i < 26; ++i) {
                        if (drives & (1 << i)) {
                            std::wstring drv = { (wchar_t)('A' + i), L':', L'\\', L'\0' };
                            FileShredder::wipeFreeSpace(drv, mode, [hWnd](int p) {
                                SendDlgItemMessage(hWnd, IDC_PROG_SHRED, PBM_SETPOS, p, 0);
                            }, nullptr);
                        }
                    }
                } else {
                    FileShredder::wipeFreeSpace(driveW, mode, [hWnd](int p) {
                        SendDlgItemMessage(hWnd, IDC_PROG_SHRED, PBM_SETPOS, p, 0);
                    }, nullptr);
                }
                MessageBoxW(hWnd, L"빈 공간 삭제 완료.", L"알림", MB_OK);
                SendDlgItemMessage(hWnd, IDC_PROG_SHRED, PBM_SETPOS, 0, 0);
            }).detach();
        }
    }
    return (INT_PTR)FALSE;
}

// -----------------------------------------------------------------------------
// Browser Cleaner Tab
// -----------------------------------------------------------------------------
INT_PTR CALLBACK MainWindow::BrowserDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    MainWindow* pThis = nullptr;
    if (message == WM_INITDIALOG) {
        pThis = (MainWindow*)lParam;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
        
        CheckDlgButton(hWnd, IDC_CHK_CHROME, BST_CHECKED);
        CheckDlgButton(hWnd, IDC_CHK_EDGE, BST_CHECKED);
        CheckDlgButton(hWnd, IDC_CHK_HISTORY, BST_CHECKED);
        
        return (INT_PTR)TRUE;
    } else {
        pThis = (MainWindow*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    }

    if (pThis) return pThis->HandleBrowserMessage(hWnd, message, wParam, lParam);
    return (INT_PTR)FALSE;
}

INT_PTR MainWindow::HandleBrowserMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_COMMAND && LOWORD(wParam) == IDC_BTN_CLEAN_BROWSER) {
        std::vector<BrowserType> browsers;
        if (IsDlgButtonChecked(hWnd, IDC_CHK_CHROME) == BST_CHECKED) browsers.push_back(BrowserType::Chrome);
        if (IsDlgButtonChecked(hWnd, IDC_CHK_EDGE) == BST_CHECKED) browsers.push_back(BrowserType::Edge);
        if (IsDlgButtonChecked(hWnd, IDC_CHK_FIREFOX) == BST_CHECKED) browsers.push_back(BrowserType::Firefox);
        
        std::vector<BrowserDataType> dataTypes;
        if (IsDlgButtonChecked(hWnd, IDC_CHK_CACHE) == BST_CHECKED) dataTypes.push_back(BrowserDataType::Cache);
        if (IsDlgButtonChecked(hWnd, IDC_CHK_COOKIES) == BST_CHECKED) dataTypes.push_back(BrowserDataType::Cookies);
        if (IsDlgButtonChecked(hWnd, IDC_CHK_HISTORY) == BST_CHECKED) dataTypes.push_back(BrowserDataType::History);
        if (IsDlgButtonChecked(hWnd, IDC_CHK_DOWNLOADS) == BST_CHECKED) dataTypes.push_back(BrowserDataType::Downloads);
        
        if (browsers.empty() || dataTypes.empty()) {
            MessageBoxW(hWnd, L"최소 하나 이상의 브라우저와 데이터 종류를 선택해주세요.", L"알림", MB_OK | MB_ICONWARNING);
            return (INT_PTR)TRUE;
        }

        EnableWindow(GetDlgItem(hWnd, IDC_BTN_CLEAN_BROWSER), FALSE);
        
        std::thread([this, hWnd, browsers, dataTypes]() {
            for (auto b : browsers) {
                BrowserCleaner::cleanBrowserData(b, dataTypes, ShredPass::Pass_1, [hWnd](int progress) {
                    SendDlgItemMessage(hWnd, IDC_PROG_BROWSER, PBM_SETPOS, progress, 0);
                });
            }
            SendDlgItemMessage(hWnd, IDC_PROG_BROWSER, PBM_SETPOS, 100, 0);
            MessageBoxW(hWnd, L"브라우저 청소가 완료되었습니다.", L"알림", MB_OK | MB_ICONINFORMATION);
            EnableWindow(GetDlgItem(hWnd, IDC_BTN_CLEAN_BROWSER), TRUE);
            SendDlgItemMessage(hWnd, IDC_PROG_BROWSER, PBM_SETPOS, 0, 0);
        }).detach();
    }
    return (INT_PTR)FALSE;
}

// -----------------------------------------------------------------------------
// Recovery Tab
// -----------------------------------------------------------------------------
INT_PTR CALLBACK MainWindow::RecoveryDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    MainWindow* pThis = nullptr;
    if (message == WM_INITDIALOG) {
        pThis = (MainWindow*)lParam;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
        
        pThis->PopulateDrivesCombo(GetDlgItem(hWnd, IDC_CMB_DRIVE));
        
        HWND hTree = GetDlgItem(hWnd, IDC_TREE_RECOVERY);
        // Set extended style if needed, but standard tree view is fine.
        
        return (INT_PTR)TRUE;
    } else {
        pThis = (MainWindow*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    }

    if (pThis) {
        if (message == WM_NOTIFY) {
            LPNMHDR lpnmh = (LPNMHDR)lParam;
            if (lpnmh->idFrom == IDC_TREE_RECOVERY) {
                if (lpnmh->code == NM_CUSTOMDRAW) {
                    LPNMTVCUSTOMDRAW pNMTVCD = (LPNMTVCUSTOMDRAW)lParam;
                    switch (pNMTVCD->nmcd.dwDrawStage) {
                    case CDDS_PREPAINT:
                        return CDRF_NOTIFYITEMDRAW;
                    case CDDS_ITEMPREPAINT: {
                        HTREEITEM hItem = (HTREEITEM)pNMTVCD->nmcd.dwItemSpec;
                        TVITEMW tvi;
                        tvi.mask = TVIF_PARAM;
                        tvi.hItem = hItem;
                        TreeView_GetItem(GetDlgItem(hWnd, IDC_TREE_RECOVERY), &tvi);
                        
                        if (tvi.lParam != -1) { // It's a file
                            int index = (int)tvi.lParam;
                            if (index >= 0 && index < pThis->m_recoveredFiles.size()) {
                                const auto& rf = pThis->m_recoveredFiles[index];
                                if (rf.recoverability == L"High") {
                                    pNMTVCD->clrText = RGB(0, 128, 0); // Green
                                } else if (rf.recoverability == L"Low") {
                                    pNMTVCD->clrText = RGB(128, 128, 0); // Yellow/Olive
                                } else {
                                    pNMTVCD->clrText = RGB(255, 0, 0); // Red
                                }
                            }
                        } else {
                            // Folder
                            pNMTVCD->clrText = RGB(0, 0, 0); // Black
                        }
                        return CDRF_DODEFAULT;
                    }
                    }
                } else if (lpnmh->code == NM_RCLICK) {
                    POINT pt;
                    GetCursorPos(&pt);
                    pThis->ShowRecoveryContextMenu(hWnd, pt);
                }
            }
        }
        return pThis->HandleRecoveryMessage(hWnd, message, wParam, lParam);
    }
    return (INT_PTR)FALSE;
}

INT_PTR MainWindow::HandleRecoveryMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_COMMAND) {
        int wmId = LOWORD(wParam);
        if (wmId == IDC_BTN_SCAN) {
            HWND hCombo = GetDlgItem(hWnd, IDC_CMB_DRIVE);
            int sel = ComboBox_GetCurSel(hCombo);
            if (sel == CB_ERR) return (INT_PTR)TRUE;
            
            wchar_t driveStr[128];
            ComboBox_GetLBText(hCombo, sel, driveStr);
            std::wstring drive(driveStr);
            
            EnableWindow(GetDlgItem(hWnd, IDC_BTN_SCAN), FALSE);
            TreeView_DeleteAllItems(GetDlgItem(hWnd, IDC_TREE_RECOVERY));
            SetDlgItemTextW(hWnd, IDC_LBL_REC_STATUS, L"Scanning...");
            
            std::thread([this, hWnd, drive]() {
                m_recoveredFiles.clear();
                
                auto progressCallback = [hWnd](int p, const std::wstring& msg) {
                    SendDlgItemMessage(hWnd, IDC_PROG_REC, PBM_SETPOS, p, 0);
                    SetDlgItemTextW(hWnd, IDC_LBL_REC_STATUS, msg.c_str());
                };
                
                if (drive.find(L"All Drives") != std::wstring::npos) {
                    DWORD drives = GetLogicalDrives();
                    for (int i = 0; i < 26; ++i) {
                        if (drives & (1 << i)) {
                            std::wstring drv = { (wchar_t)('A' + i), L':', L'\\', L'\0' };
                            NtfsRecovery ntfs;
                            if (!ntfs.scanDrive(drv, m_recoveredFiles, progressCallback)) {
                                FatRecovery fat;
                                fat.scanDrive(drv, m_recoveredFiles, progressCallback);
                            }
                        }
                    }
                } else {
                    NtfsRecovery ntfs;
                    if (!ntfs.scanDrive(drive, m_recoveredFiles, progressCallback)) {
                        FatRecovery fat;
                        fat.scanDrive(drive, m_recoveredFiles, progressCallback);
                    }
                }
                
                // Sort by recoverability
                std::sort(m_recoveredFiles.begin(), m_recoveredFiles.end(), [](const RecoverableFile& a, const RecoverableFile& b) {
                    if (a.recoverability == L"High" && b.recoverability != L"High") return true;
                    return false;
                });
                
                PopulateRecoveryTree(GetDlgItem(hWnd, IDC_TREE_RECOVERY));
                EnableWindow(GetDlgItem(hWnd, IDC_BTN_SCAN), TRUE);
                SetDlgItemTextW(hWnd, IDC_LBL_REC_STATUS, L"Scan complete.");
                SendDlgItemMessage(hWnd, IDC_PROG_REC, PBM_SETPOS, 0, 0);
            }).detach();
        } else if (wmId == IDC_BTN_RECOVER) {
            HWND hTree = GetDlgItem(hWnd, IDC_TREE_RECOVERY);
            HTREEITEM hItem = TreeView_GetSelection(hTree);
            if (!hItem) return (INT_PTR)TRUE;
            
            TVITEMW tvi;
            tvi.mask = TVIF_PARAM;
            tvi.hItem = hItem;
            TreeView_GetItem(hTree, &tvi);
            
            if (tvi.lParam == -1) return (INT_PTR)TRUE; // Folder
            int sel = (int)tvi.lParam;
            
            if (sel >= 0 && sel < (int)m_recoveredFiles.size()) {
                const auto& rf = m_recoveredFiles[sel];
                
                IFileOpenDialog* pFolderOpen;
                if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFolderOpen)))) {
                    DWORD dwOptions;
                    pFolderOpen->GetOptions(&dwOptions);
                    pFolderOpen->SetOptions(dwOptions | FOS_PICKFOLDERS);
                    
                    if (SUCCEEDED(pFolderOpen->Show(hWnd))) {
                        IShellItem* pItem;
                        if (SUCCEEDED(pFolderOpen->GetResult(&pItem))) {
                            PWSTR pszFolderPath;
                            if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFolderPath))) {
                                std::wstring destFolder = pszFolderPath;
                                CoTaskMemFree(pszFolderPath);
                                
                                std::wstring destPath = destFolder + L"\\" + rf.name;
                                
                                // Need original drive path
                                HWND hCombo = GetDlgItem(hWnd, IDC_CMB_DRIVE);
                                int drvSel = ComboBox_GetCurSel(hCombo);
                                wchar_t driveStr[10];
                                ComboBox_GetLBText(hCombo, drvSel, driveStr);
                                
                                NtfsRecovery ntfs;
                                if (ntfs.recoverFile(driveStr, rf, destPath)) {
                                    MessageBoxW(hWnd, L"파일 복구가 완료되었습니다.", L"성공", MB_OK);
                                } else {
                                    MessageBoxW(hWnd, L"파일 복구에 실패했습니다. (지원되지 않는 파일 시스템이거나 덮어써짐)", L"실패", MB_OK | MB_ICONERROR);
                                }
                            }
                            pItem->Release();
                        }
                    }
                    pFolderOpen->Release();
                }
            }
        } else if (HIWORD(wParam) == EN_CHANGE && wmId == IDC_EDIT_REC_SEARCH) {
            wchar_t searchBuf[256];
            GetDlgItemTextW(hWnd, IDC_EDIT_REC_SEARCH, searchBuf, 256);
            FilterRecoveryTree(GetDlgItem(hWnd, IDC_TREE_RECOVERY), searchBuf);
        }
    }
    return (INT_PTR)FALSE;
}

void MainWindow::PopulateDrivesCombo(HWND hCombo) {
    ComboBox_AddString(hCombo, L"모든 드라이브 (All Drives)");
    DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (drives & (1 << i)) {
            wchar_t drive[] = { (wchar_t)('A' + i), L':', L'\\', L'\0' };
            ComboBox_AddString(hCombo, drive);
        }
    }
    ComboBox_SetCurSel(hCombo, 0);
}

static HTREEITEM InsertTreeItem(HWND hTree, HTREEITEM hParent, const std::wstring& text, LPARAM lParam) {
    TVINSERTSTRUCTW tvis = {0};
    tvis.hParent = hParent;
    tvis.hInsertAfter = TVI_LAST;
    tvis.item.mask = TVIF_TEXT | TVIF_PARAM;
    tvis.item.pszText = (LPWSTR)text.c_str();
    tvis.item.lParam = lParam;
    return TreeView_InsertItem(hTree, &tvis);
}

void MainWindow::PopulateRecoveryTree(HWND hTree) {
    TreeView_DeleteAllItems(hTree);
    FilterRecoveryTree(hTree, L"");
}

void MainWindow::FilterRecoveryTree(HWND hTree, const std::wstring& filter) {
    TreeView_DeleteAllItems(hTree);
    
    std::wstring lowerFilter = filter;
    std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::towlower);
    
    std::map<std::wstring, HTREEITEM> folderMap;
    
    for (size_t i = 0; i < m_recoveredFiles.size(); ++i) {
        const auto& rf = m_recoveredFiles[i];
        
        std::wstring nameLower = rf.name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::towlower);
        
        if (!lowerFilter.empty() && nameLower.find(lowerFilter) == std::wstring::npos) {
            continue;
        }
        
        // Split path to create folders
        std::wstring path = rf.fullPath;
        std::wstring currentFolder = L"";
        HTREEITEM hParent = TVI_ROOT;
        
        size_t pos = 0;
        while ((pos = path.find(L'/')) != std::wstring::npos) {
            std::wstring folderName = path.substr(0, pos);
            if (!folderName.empty()) {
                currentFolder += folderName + L"/";
                if (folderMap.find(currentFolder) == folderMap.end()) {
                    hParent = InsertTreeItem(hTree, hParent, folderName, -1);
                    folderMap[currentFolder] = hParent;
                } else {
                    hParent = folderMap[currentFolder];
                }
            }
            path.erase(0, pos + 1);
        }
        
        // Insert file
        std::wstring displayStr = rf.name + L" (" + rf.recoverability + L", " + std::to_wstring(rf.size) + L" bytes)";
        InsertTreeItem(hTree, hParent, displayStr, static_cast<LPARAM>(i));
    }
}

void MainWindow::ShowRecoveryContextMenu(HWND hWnd, POINT pt) {
    HWND hTree = GetDlgItem(hWnd, IDC_TREE_RECOVERY);
    
    // Convert to client coordinates to get hit test
    POINT ptClient = pt;
    ScreenToClient(hTree, &ptClient);
    
    TVHITTESTINFO tvht = {0};
    tvht.pt = ptClient;
    HTREEITEM hItem = TreeView_HitTest(hTree, &tvht);
    
    if (hItem) {
        TreeView_SelectItem(hTree, hItem);
        
        TVITEMW tvi;
        tvi.mask = TVIF_PARAM;
        tvi.hItem = hItem;
        TreeView_GetItem(hTree, &tvi);
        
        if (tvi.lParam != -1) { // It's a file
            HMENU hMenu = CreatePopupMenu();
            InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, IDC_BTN_RECOVER, L"선택 항목 복구 (Recover)");
            
            int ret = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hWnd, NULL);
            DestroyMenu(hMenu);
            
            if (ret != 0) {
                SendMessageW(hWnd, WM_COMMAND, MAKEWPARAM(ret, 0), 0);
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Config Dialog (Settings)
// -----------------------------------------------------------------------------
INT_PTR CALLBACK MainWindow::ConfigDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INITDIALOG) {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\clearMax", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            DWORD val;
            DWORD size = sizeof(DWORD);
            
            auto loadCheck = [&](const wchar_t* name, int id, bool defaultVal) {
                val = 0; size = sizeof(DWORD);
                if (RegQueryValueExW(hKey, name, NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS)
                    CheckDlgButton(hWnd, id, val ? BST_CHECKED : BST_UNCHECKED);
                else
                    CheckDlgButton(hWnd, id, defaultVal ? BST_CHECKED : BST_UNCHECKED);
            };
            
            loadCheck(L"CfgHistory", IDC_CHK_CFG_HISTORY, true);
            loadCheck(L"CfgCache", IDC_CHK_CFG_CACHE, false);
            loadCheck(L"CfgCookies", IDC_CHK_CFG_COOKIES, false);
            loadCheck(L"CfgDownloads", IDC_CHK_CFG_DOWNLOADS, false);
            loadCheck(L"CfgAllDrives", IDC_CHK_CFG_ALL_DRIVES, true);
            loadCheck(L"CfgMftOnly", IDC_CHK_CFG_MFT_ONLY, true);
            
            RegCloseKey(hKey);
        } else {
            CheckDlgButton(hWnd, IDC_CHK_CFG_HISTORY, BST_CHECKED);
            CheckDlgButton(hWnd, IDC_CHK_CFG_CACHE, BST_UNCHECKED);
            CheckDlgButton(hWnd, IDC_CHK_CFG_COOKIES, BST_UNCHECKED);
            CheckDlgButton(hWnd, IDC_CHK_CFG_DOWNLOADS, BST_UNCHECKED);
            CheckDlgButton(hWnd, IDC_CHK_CFG_ALL_DRIVES, BST_CHECKED);
            CheckDlgButton(hWnd, IDC_CHK_CFG_MFT_ONLY, BST_CHECKED);
        }
        return (INT_PTR)TRUE;
    } else if (message == WM_COMMAND) {
        if (LOWORD(wParam) == IDC_BTN_SAVE_CONFIG) {
            HKEY hKey;
            if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\clearMax", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
                auto saveCheck = [&](const wchar_t* name, int id) {
                    DWORD v = IsDlgButtonChecked(hWnd, id) == BST_CHECKED ? 1 : 0;
                    RegSetValueExW(hKey, name, 0, REG_DWORD, (const BYTE*)&v, sizeof(DWORD));
                };
                
                saveCheck(L"CfgHistory", IDC_CHK_CFG_HISTORY);
                saveCheck(L"CfgCache", IDC_CHK_CFG_CACHE);
                saveCheck(L"CfgCookies", IDC_CHK_CFG_COOKIES);
                saveCheck(L"CfgDownloads", IDC_CHK_CFG_DOWNLOADS);
                saveCheck(L"CfgAllDrives", IDC_CHK_CFG_ALL_DRIVES);
                saveCheck(L"CfgMftOnly", IDC_CHK_CFG_MFT_ONLY);
                
                RegCloseKey(hKey);
            }
            EndDialog(hWnd, LOWORD(wParam));
            return (INT_PTR)TRUE;
        } else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hWnd, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
    } else if (message == WM_CLOSE) {
        EndDialog(hWnd, IDCANCEL);
        return (INT_PTR)TRUE;
    }
    return (INT_PTR)FALSE;
}

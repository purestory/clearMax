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
#include <chrono>
#include <shellapi.h>

MainWindow::MainWindow(HINSTANCE hInstance) 
    : m_hInstance(hInstance), m_hWnd(NULL), m_hTabControl(NULL),
      m_hTabPrograms(NULL), m_hTabShredder(NULL), m_hTabBrowser(NULL), m_hTabRecovery(NULL), m_hTabSettings(NULL) {
    m_hProgImageList = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 0, 100);
}

MainWindow::~MainWindow() {
    if (m_hProgImageList) ImageList_Destroy(m_hProgImageList);
}

bool MainWindow::Initialize() {
    m_hWnd = CreateDialogParamW(m_hInstance, MAKEINTRESOURCEW(IDD_MAIN_DIALOG), NULL, MainDlgProc, (LPARAM)this);
    if (!m_hWnd) return false;
    return true;
}

void MainWindow::Show() {
    if (m_hWnd) {
        ShowWindow(m_hWnd, SW_RESTORE);
        SetForegroundWindow(m_hWnd);
        BringWindowToTop(m_hWnd);
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
        case WM_GETMINMAXINFO: {
            LPMINMAXINFO lpMMI = (LPMINMAXINFO)lParam;
            lpMMI->ptMinTrackSize.x = 420;
            lpMMI->ptMinTrackSize.y = 350;
            return 0;
        }
        case WM_SIZE: {
            if (wParam != SIZE_MINIMIZED && m_hTabControl) {
                RECT rcClient;
                GetClientRect(hWnd, &rcClient);
                SetWindowPos(m_hTabControl, NULL, 5, 5, rcClient.right - 10, rcClient.bottom - 10, SWP_NOZORDER);
                ResizeTabs();
            }
            return (INT_PTR)TRUE;
        }
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

    tie.pszText = (LPWSTR)L"환경 설정";
    TabCtrl_InsertItem(m_hTabControl, 4, &tie);

    m_hTabPrograms = CreateDialogParamW(m_hInstance, MAKEINTRESOURCEW(IDD_TAB_PROGRAMS), m_hTabControl, ProgramsDlgProc, (LPARAM)this);
    m_hTabShredder = CreateDialogParamW(m_hInstance, MAKEINTRESOURCEW(IDD_TAB_SHREDDER), m_hTabControl, ShredderDlgProc, (LPARAM)this);
    m_hTabBrowser  = CreateDialogParamW(m_hInstance, MAKEINTRESOURCEW(IDD_TAB_BROWSER), m_hTabControl, BrowserDlgProc, (LPARAM)this);
    m_hTabRecovery = CreateDialogParamW(m_hInstance, MAKEINTRESOURCEW(IDD_TAB_RECOVERY), m_hTabControl, RecoveryDlgProc, (LPARAM)this);
    m_hTabSettings = CreateDialogParamW(m_hInstance, MAKEINTRESOURCEW(IDD_TAB_SETTINGS), m_hTabControl, SettingsDlgProc, (LPARAM)this);

    ResizeTabs();
    OnTabChanged();
}

void MainWindow::ResizeTabs() {
    RECT rcClient, rcTab;
    GetClientRect(m_hTabControl, &rcClient);
    TabCtrl_AdjustRect(m_hTabControl, FALSE, &rcClient);

    HWND tabs[] = { m_hTabPrograms, m_hTabShredder, m_hTabBrowser, m_hTabRecovery, m_hTabSettings };
    for (int i = 0; i < 5; ++i) {
        SetWindowPos(tabs[i], NULL, rcClient.left, rcClient.top, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top, SWP_NOZORDER);
    }
}

void MainWindow::OnTabChanged() {
    int sel = TabCtrl_GetCurSel(m_hTabControl);
    HWND tabs[] = { m_hTabPrograms, m_hTabShredder, m_hTabBrowser, m_hTabRecovery, m_hTabSettings };
    
    for (int i = 0; i < 5; ++i) {
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
        
        ListView_SetImageList(hList, pThis->m_hProgImageList, LVSIL_SMALL);
        pThis->PopulateProgramsList(hList);
        
        return (INT_PTR)TRUE;
    } else {
        pThis = (MainWindow*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    }

    if (pThis) return pThis->HandleProgramsMessage(hWnd, message, wParam, lParam);
    return (INT_PTR)FALSE;
}

INT_PTR MainWindow::HandleProgramsMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_SIZE) {
        if (wParam != SIZE_MINIMIZED) {
            RECT rcClient; GetClientRect(hWnd, &rcClient);
            int w = rcClient.right, h = rcClient.bottom;
            
            RECT rDLU = { 10, 30, 40, 120 }; MapDialogRect(hWnd, &rDLU);
            int m10 = rDLU.left, m30 = rDLU.top, m40 = rDLU.right, m120 = rDLU.bottom;

            HWND hSearch = GetDlgItem(hWnd, IDC_EDIT_PROG_SEARCH);
            if (hSearch) {
                RECT rcS; GetWindowRect(hSearch, &rcS); MapWindowPoints(HWND_DESKTOP, hWnd, (LPPOINT)&rcS, 2);
                SetWindowPos(hSearch, NULL, 0, 0, w - rcS.left - m10, rcS.bottom - rcS.top, SWP_NOMOVE | SWP_NOZORDER);
            }
            HWND hList = GetDlgItem(hWnd, IDC_LIST_PROGRAMS);
            if (hList) {
                RECT rcL; GetWindowRect(hList, &rcL); MapWindowPoints(HWND_DESKTOP, hWnd, (LPPOINT)&rcL, 2);
                SetWindowPos(hList, NULL, 0, 0, w - rcL.left - m10, h - rcL.top - m40, SWP_NOMOVE | SWP_NOZORDER);
            }
            
            int btnY = h - m30;
            HWND hBtnRefresh = GetDlgItem(hWnd, IDC_BTN_PROG_REFRESH);
            if (hBtnRefresh) {
                RECT rcB; GetWindowRect(hBtnRefresh, &rcB); MapWindowPoints(HWND_DESKTOP, hWnd, (LPPOINT)&rcB, 2);
                SetWindowPos(hBtnRefresh, NULL, rcB.left, btnY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            }
            
            HWND hBtnForce = GetDlgItem(hWnd, IDC_BTN_FORCE_REMOVE);
            if (hBtnForce) {
                RECT rcF; GetWindowRect(hBtnForce, &rcF); int fW = rcF.right - rcF.left;
                SetWindowPos(hBtnForce, NULL, w - m10 - fW, btnY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            }
            
            HWND hBtnUn = GetDlgItem(hWnd, IDC_BTN_UNINSTALL);
            if (hBtnUn) {
                RECT rcU; GetWindowRect(hBtnUn, &rcU); int uW = rcU.right - rcU.left;
                HWND hF = GetDlgItem(hWnd, IDC_BTN_FORCE_REMOVE);
                RECT rcF; GetWindowRect(hF, &rcF); int fW = rcF.right - rcF.left;
                SetWindowPos(hBtnUn, NULL, w - m10 - fW - m10 - uW, btnY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            }
        }
        return (INT_PTR)TRUE;
    } else if (message == WM_COMMAND) {
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
                        RegistryMgr::uninstallProgram(info, [hWnd]() {
                            PostMessageW(hWnd, WM_COMMAND, MAKEWPARAM(IDC_BTN_PROG_REFRESH, 0), 0);
                        });
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
    
    ImageList_RemoveAll(m_hProgImageList);
    HICON hDefaultIcon = LoadIconW(NULL, IDI_APPLICATION);
    int defaultIconIdx = ImageList_AddIcon(m_hProgImageList, hDefaultIcon);

    for (auto& prog : m_programs) {
        if (!prog.displayIcon.empty()) {
            std::wstring iconPath = prog.displayIcon;
            int iconIndex = 0;
            
            // Handle "path,index" format
            size_t commaPos = iconPath.find_last_of(L',');
            if (commaPos != std::wstring::npos) {
                std::wstring idxStr = iconPath.substr(commaPos + 1);
                bool isNum = true;
                for(wchar_t c : idxStr) {
                    if(!iswdigit(c) && c != L'-') { isNum = false; break; }
                }
                if (isNum && !idxStr.empty()) {
                    iconIndex = _wtoi(idxStr.c_str());
                    iconPath = iconPath.substr(0, commaPos);
                }
            }
            
            // Remove quotes if present
            if (!iconPath.empty() && iconPath.front() == L'"' && iconPath.back() == L'"') {
                iconPath = iconPath.substr(1, iconPath.length() - 2);
            }
            
            HICON hIcon = NULL;
            if (ExtractIconExW(iconPath.c_str(), iconIndex, NULL, &hIcon, 1) > 0 && hIcon != NULL) {
                prog.iconIndex = ImageList_AddIcon(m_hProgImageList, hIcon);
                DestroyIcon(hIcon);
            } else {
                // If extraction failed but path exists, try SHGetFileInfo
                SHFILEINFOW sfi = {0};
                if (SHGetFileInfoW(iconPath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON)) {
                    prog.iconIndex = ImageList_AddIcon(m_hProgImageList, sfi.hIcon);
                    DestroyIcon(sfi.hIcon);
                } else {
                    prog.iconIndex = defaultIconIdx;
                }
            }
        } else {
            prog.iconIndex = defaultIconIdx;
        }
    }

    FilterProgramsList(hList, L"");
}

void MainWindow::FilterProgramsList(HWND hList, const std::wstring& filter) {
    SendMessageW(hList, WM_SETREDRAW, FALSE, 0);

    int topIndex = ListView_GetTopIndex(hList);
    int selItem = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
    std::wstring selName;
    if (selItem != -1) {
        wchar_t buf[256] = {0};
        ListView_GetItemText(hList, selItem, 0, buf, 256);
        selName = buf;
    }

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
        lvi.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
        lvi.iItem = row;
        lvi.iSubItem = 0;
        lvi.iImage = prog.iconIndex;
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

    if (m_sortColumn != -1) {
        ListView_SortItems(hList, MainWindow::ListViewCompareProc, (LPARAM)this);
    }

    if (!selName.empty()) {
        int count = ListView_GetItemCount(hList);
        for (int i = 0; i < count; ++i) {
            wchar_t buf[256] = {0};
            ListView_GetItemText(hList, i, 0, buf, 256);
            if (selName == buf) {
                ListView_SetItemState(hList, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                break;
            }
        }
    }

    if (topIndex > 0) {
        int perPage = ListView_GetCountPerPage(hList);
        int targetBottom = topIndex + perPage - 1;
        int count = ListView_GetItemCount(hList);
        if (targetBottom >= count) targetBottom = count - 1;
        if (targetBottom >= 0) {
            ListView_EnsureVisible(hList, targetBottom, FALSE);
            ListView_EnsureVisible(hList, topIndex, FALSE);
        }
    }

    SendMessageW(hList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hList, NULL, TRUE);
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
    if (message == WM_SIZE) {
        if (wParam != SIZE_MINIMIZED) {
            RECT rcClient; GetClientRect(hWnd, &rcClient);
            int w = rcClient.right, h = rcClient.bottom;
            RECT rDLU = { 10, 25, 50, 0 }; MapDialogRect(hWnd, &rDLU);
            int m10 = rDLU.left, m25 = rDLU.top, m50 = rDLU.right;
            RECT rDLU3 = { 0, 2, 10, 0 }; MapDialogRect(hWnd, &rDLU3);
            int m2 = rDLU3.top, gap = rDLU3.right;
            
            HWND hProg = GetDlgItem(hWnd, IDC_PROG_SHRED);
            if (hProg) {
                RECT rcP; GetWindowRect(hProg, &rcP); MapWindowPoints(HWND_DESKTOP, hWnd, (LPPOINT)&rcP, 2);
                SetWindowPos(hProg, NULL, rcP.left, h - m25, w - rcP.left - m10, rcP.bottom - rcP.top, SWP_NOZORDER);
            }
            HWND hStop = GetDlgItem(hWnd, IDC_BTN_SHRED_STOP);
            int bw = 75;
            if (hStop) {
                RECT rcS; GetWindowRect(hStop, &rcS); bw = rcS.right - rcS.left;
                SetWindowPos(hStop, NULL, w - m10 - bw, h - m50 + m2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            }
            HWND hPause = GetDlgItem(hWnd, IDC_BTN_SHRED_PAUSE);
            if (hPause) {
                SetWindowPos(hPause, NULL, w - m10 - bw - gap - bw, h - m50 + m2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            }
            HWND hLbl = GetDlgItem(hWnd, IDC_LBL_SHRED_STATUS);
            if (hLbl) {
                RECT rcL; GetWindowRect(hLbl, &rcL); MapWindowPoints(HWND_DESKTOP, hWnd, (LPPOINT)&rcL, 2);
                SetWindowPos(hLbl, NULL, rcL.left, h - m50, w - rcL.left - m10 - bw - gap - bw - gap, rcL.bottom - rcL.top, SWP_NOZORDER);
            }
        }
        return (INT_PTR)TRUE;
    } else if (message == WM_COMMAND) {
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
                                m_cancelShred = false;
                                m_pauseShred = false;
                                EnableWindow(GetDlgItem(hWnd, IDC_BTN_SHRED_FILE), FALSE);
                                EnableWindow(GetDlgItem(hWnd, IDC_BTN_SHRED_FOLDER), FALSE);
                                EnableWindow(GetDlgItem(hWnd, IDC_BTN_WIPE_FREESPACE), FALSE);
                                ShowWindow(GetDlgItem(hWnd, IDC_BTN_SHRED_PAUSE), SW_SHOW);
                                ShowWindow(GetDlgItem(hWnd, IDC_BTN_SHRED_STOP), SW_SHOW);
                                SetDlgItemTextW(hWnd, IDC_BTN_SHRED_PAUSE, L"일시정지");
                                
                                std::thread([this, hWnd, path, passes, wmId]() {
                                    auto cancelCheck = [this]() -> bool {
                                        while (m_pauseShred) {
                                            if (m_cancelShred) return true;
                                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                                        }
                                        return m_cancelShred;
                                    };
                                    
                                    FileShredder::shredPath(path, passes, [hWnd](int p, const std::wstring&) {
                                        SendDlgItemMessage(hWnd, IDC_PROG_SHRED, PBM_SETPOS, p, 0);
                                    }, cancelCheck);
                                    
                                    if (m_cancelShred) MessageBoxW(hWnd, L"파쇄 작업이 취소되었습니다.", L"알림", MB_OK);
                                    else MessageBoxW(hWnd, L"파쇄 완료.", L"알림", MB_OK);
                                    
                                    SendDlgItemMessage(hWnd, IDC_PROG_SHRED, PBM_SETPOS, 0, 0);
                                    EnableWindow(GetDlgItem(hWnd, IDC_BTN_SHRED_FILE), TRUE);
                                    EnableWindow(GetDlgItem(hWnd, IDC_BTN_SHRED_FOLDER), TRUE);
                                    EnableWindow(GetDlgItem(hWnd, IDC_BTN_WIPE_FREESPACE), TRUE);
                                    ShowWindow(GetDlgItem(hWnd, IDC_BTN_SHRED_PAUSE), SW_HIDE);
                                    ShowWindow(GetDlgItem(hWnd, IDC_BTN_SHRED_STOP), SW_HIDE);
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
            
            m_cancelShred = false;
            m_pauseShred = false;
            EnableWindow(GetDlgItem(hWnd, IDC_BTN_SHRED_FILE), FALSE);
            EnableWindow(GetDlgItem(hWnd, IDC_BTN_SHRED_FOLDER), FALSE);
            EnableWindow(GetDlgItem(hWnd, IDC_BTN_WIPE_FREESPACE), FALSE);
            ShowWindow(GetDlgItem(hWnd, IDC_BTN_SHRED_PAUSE), SW_SHOW);
            ShowWindow(GetDlgItem(hWnd, IDC_BTN_SHRED_STOP), SW_SHOW);
            SetDlgItemTextW(hWnd, IDC_BTN_SHRED_PAUSE, L"일시정지");

            std::wstring driveW(driveStr);
            std::thread([this, hWnd, driveW, mode]() {
                auto cancelCheck = [this]() -> bool {
                    while (m_pauseShred) {
                        if (m_cancelShred) return true;
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                    return m_cancelShred;
                };

                auto startTime = std::chrono::steady_clock::now();
                std::vector<std::wstring> drivesToWipe;
                
                if (driveW.find(L"All Drives") != std::wstring::npos) {
                    DWORD drives = GetLogicalDrives();
                    for (int i = 0; i < 26; ++i) {
                        if (drives & (1 << i)) {
                            std::wstring drv = std::wstring(1, (wchar_t)('A' + i)) + L":\\";
                            drivesToWipe.push_back(drv);
                        }
                    }
                } else {
                    drivesToWipe.push_back(driveW);
                }
                
                int totalDrives = static_cast<int>(drivesToWipe.size());
                if (totalDrives == 0) return;
                
                int currentDriveIndex = 0;
                
                for (const auto& drv : drivesToWipe) {
                    if (m_cancelShred) break;
                    auto progressCb = [hWnd, drv, currentDriveIndex, totalDrives, startTime](int p) {
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
                        
                        std::wstring statusStr = L"[" + drv + L"] 빈 공간 삭제 중...\n전체 진행률: " + std::to_wstring(overallProgress) + L"% (남은 시간: " + etaStr + L")";
                        
                        SendDlgItemMessage(hWnd, IDC_PROG_SHRED, PBM_SETPOS, overallProgress, 0);
                        SetDlgItemTextW(hWnd, IDC_LBL_SHRED_STATUS, statusStr.c_str());
                    };
                    
                    FileShredder::wipeFreeSpace(drv, mode, progressCb, cancelCheck);
                    currentDriveIndex++;
                }
                
                if (m_cancelShred) MessageBoxW(hWnd, L"빈 공간 삭제 작업이 취소되었습니다.", L"알림", MB_OK);
                else MessageBoxW(hWnd, L"빈 공간 삭제 완료.", L"알림", MB_OK);

                SetDlgItemTextW(hWnd, IDC_LBL_SHRED_STATUS, L"대기 중");
                SendDlgItemMessage(hWnd, IDC_PROG_SHRED, PBM_SETPOS, 0, 0);
                
                EnableWindow(GetDlgItem(hWnd, IDC_BTN_SHRED_FILE), TRUE);
                EnableWindow(GetDlgItem(hWnd, IDC_BTN_SHRED_FOLDER), TRUE);
                EnableWindow(GetDlgItem(hWnd, IDC_BTN_WIPE_FREESPACE), TRUE);
                ShowWindow(GetDlgItem(hWnd, IDC_BTN_SHRED_PAUSE), SW_HIDE);
                ShowWindow(GetDlgItem(hWnd, IDC_BTN_SHRED_STOP), SW_HIDE);
            }).detach();
        } else if (wmId == IDC_BTN_SHRED_PAUSE) {
            m_pauseShred = !m_pauseShred;
            SetDlgItemTextW(hWnd, IDC_BTN_SHRED_PAUSE, m_pauseShred ? L"계속" : L"일시정지");
        } else if (wmId == IDC_BTN_SHRED_STOP) {
            m_cancelShred = true;
            m_pauseShred = false; // resume so it can exit
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
    if (message == WM_SIZE) {
        if (wParam != SIZE_MINIMIZED) {
            RECT rcClient; GetClientRect(hWnd, &rcClient);
            RECT rDLU = { 10, 20, 0, 0 }; MapDialogRect(hWnd, &rDLU);
            HWND hProg = GetDlgItem(hWnd, IDC_PROG_BROWSER);
            if (hProg) {
                RECT rcP; GetWindowRect(hProg, &rcP); MapWindowPoints(HWND_DESKTOP, hWnd, (LPPOINT)&rcP, 2);
                SetWindowPos(hProg, NULL, rcP.left, rcClient.bottom - rDLU.top, rcClient.right - rcP.left - rDLU.left, rcP.bottom - rcP.top, SWP_NOZORDER);
            }
        }
        return (INT_PTR)TRUE;
    } else if (message == WM_COMMAND && LOWORD(wParam) == IDC_BTN_CLEAN_BROWSER) {
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
    if (message == WM_SIZE) {
        if (wParam != SIZE_MINIMIZED) {
            RECT rcClient; GetClientRect(hWnd, &rcClient);
            int w = rcClient.right, h = rcClient.bottom;
            RECT rDLU = { 10, 65, 55, 31 }; MapDialogRect(hWnd, &rDLU);
            int m10 = rDLU.left, m65 = rDLU.top, m55 = rDLU.right, m31 = rDLU.bottom;
            RECT rDLU2 = { 18, 0, 0, 0 }; MapDialogRect(hWnd, &rDLU2);
            int m18 = rDLU2.left;

            HWND hSearch = GetDlgItem(hWnd, IDC_EDIT_REC_SEARCH);
            if (hSearch) {
                RECT rcS; GetWindowRect(hSearch, &rcS); MapWindowPoints(HWND_DESKTOP, hWnd, (LPPOINT)&rcS, 2);
                SetWindowPos(hSearch, NULL, 0, 0, w - rcS.left - m10, rcS.bottom - rcS.top, SWP_NOMOVE | SWP_NOZORDER);
            }
            HWND hTree = GetDlgItem(hWnd, IDC_TREE_RECOVERY);
            if (hTree) {
                RECT rcT; GetWindowRect(hTree, &rcT); MapWindowPoints(HWND_DESKTOP, hWnd, (LPPOINT)&rcT, 2);
                SetWindowPos(hTree, NULL, 0, 0, w - rcT.left - m10, h - rcT.top - m65, SWP_NOMOVE | SWP_NOZORDER);
            }
            
            RECT rDLU3 = { 0, 2, 10, 0 }; MapDialogRect(hWnd, &rDLU3);
            int m2 = rDLU3.top, gap = rDLU3.right;
            HWND hStop = GetDlgItem(hWnd, IDC_BTN_REC_STOP);
            int bw = 75;
            if (hStop) {
                RECT rcS; GetWindowRect(hStop, &rcS); bw = rcS.right - rcS.left;
                SetWindowPos(hStop, NULL, w - m10 - bw, h - m55 + m2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            }
            HWND hPause = GetDlgItem(hWnd, IDC_BTN_REC_PAUSE);
            if (hPause) {
                SetWindowPos(hPause, NULL, w - m10 - bw - gap - bw, h - m55 + m2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            }
            HWND hLbl = GetDlgItem(hWnd, IDC_LBL_REC_STATUS);
            if (hLbl) {
                RECT rcL; GetWindowRect(hLbl, &rcL); MapWindowPoints(HWND_DESKTOP, hWnd, (LPPOINT)&rcL, 2);
                SetWindowPos(hLbl, NULL, rcL.left, h - m55, w - rcL.left - m10 - bw - gap - bw - gap, rcL.bottom - rcL.top, SWP_NOZORDER);
            }
            HWND hProg = GetDlgItem(hWnd, IDC_PROG_REC);
            if (hProg) {
                RECT rcP; GetWindowRect(hProg, &rcP); MapWindowPoints(HWND_DESKTOP, hWnd, (LPPOINT)&rcP, 2);
                SetWindowPos(hProg, NULL, rcP.left, h - m31, w - rcP.left - m10, rcP.bottom - rcP.top, SWP_NOZORDER);
            }
            HWND hBtn = GetDlgItem(hWnd, IDC_BTN_RECOVER);
            if (hBtn) {
                RECT rcB; GetWindowRect(hBtn, &rcB); int btnW = rcB.right - rcB.left;
                SetWindowPos(hBtn, NULL, w - m10 - btnW, h - m18, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            }
        }
        return (INT_PTR)TRUE;
    } else if (message == WM_COMMAND) {
        int wmId = LOWORD(wParam);
        if (wmId == IDC_BTN_SCAN) {
            HWND hCombo = GetDlgItem(hWnd, IDC_CMB_DRIVE);
            int sel = ComboBox_GetCurSel(hCombo);
            if (sel == CB_ERR) return (INT_PTR)TRUE;
            
            wchar_t driveStr[128];
            ComboBox_GetLBText(hCombo, sel, driveStr);
            std::wstring drive(driveStr);
            
            m_cancelRecovery = false;
            m_pauseRecovery = false;
            EnableWindow(GetDlgItem(hWnd, IDC_BTN_SCAN), FALSE);
            EnableWindow(GetDlgItem(hWnd, IDC_BTN_RECOVER), FALSE);
            ShowWindow(GetDlgItem(hWnd, IDC_BTN_REC_PAUSE), SW_SHOW);
            ShowWindow(GetDlgItem(hWnd, IDC_BTN_REC_STOP), SW_SHOW);
            SetDlgItemTextW(hWnd, IDC_BTN_REC_PAUSE, L"일시정지");

            TreeView_DeleteAllItems(GetDlgItem(hWnd, IDC_TREE_RECOVERY));
            SetDlgItemTextW(hWnd, IDC_LBL_REC_STATUS, L"Scanning...");
            
            std::thread([this, hWnd, drive]() {
                auto cancelCheck = [this]() -> bool {
                    while (m_pauseRecovery) {
                        if (m_cancelRecovery) return true;
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                    return m_cancelRecovery;
                };

                m_recoveredFiles.clear();
                
                auto startTime = std::chrono::steady_clock::now();
                std::vector<std::wstring> drivesToScan;
                
                if (drive.find(L"All Drives") != std::wstring::npos) {
                    DWORD drives = GetLogicalDrives();
                    for (int i = 0; i < 26; ++i) {
                        if (drives & (1 << i)) {
                            std::wstring drv = std::wstring(1, (wchar_t)('A' + i)) + L":\\";
                            drivesToScan.push_back(drv);
                        }
                    }
                } else {
                    drivesToScan.push_back(drive);
                }
                
                int totalDrives = static_cast<int>(drivesToScan.size());
                if (totalDrives == 0) return;
                
                int currentDriveIndex = 0;
                
                for (const auto& drv : drivesToScan) {
                    auto progressCallback = [hWnd, drv, currentDriveIndex, totalDrives, startTime](int p, const std::wstring& msg) {
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
                        
                        std::wstring cleanMsg = msg;
                        if (cleanMsg.empty()) cleanMsg = L"스캔 진행 중...";
                        
                        std::wstring statusStr = L"[" + drv + L"] " + cleanMsg + L"\n전체 진행률: " + std::to_wstring(overallProgress) + L"% (남은 시간: " + etaStr + L")";
                        
                        SendDlgItemMessage(hWnd, IDC_PROG_REC, PBM_SETPOS, overallProgress, 0);
                        SetDlgItemTextW(hWnd, IDC_LBL_REC_STATUS, statusStr.c_str());
                    };
                    
                    NtfsRecovery ntfs;
                    if (!ntfs.scanDrive(drv, m_recoveredFiles, progressCallback, cancelCheck)) {
                        FatRecovery fat;
                        fat.scanDrive(drv, m_recoveredFiles, progressCallback, cancelCheck);
                    }
                    
                    if (m_cancelRecovery) break;
                    currentDriveIndex++;
                }
                
                // Sort by recoverability
                std::sort(m_recoveredFiles.begin(), m_recoveredFiles.end(), [](const RecoverableFile& a, const RecoverableFile& b) {
                    if (a.recoverability == L"High" && b.recoverability != L"High") return true;
                    return false;
                });
                
                PopulateRecoveryTree(GetDlgItem(hWnd, IDC_TREE_RECOVERY));
                EnableWindow(GetDlgItem(hWnd, IDC_BTN_SCAN), TRUE);
                if (m_cancelRecovery) {
                    SetDlgItemTextW(hWnd, IDC_LBL_REC_STATUS, L"스캔 작업이 취소되었습니다.");
                } else {
                    SetDlgItemTextW(hWnd, IDC_LBL_REC_STATUS, (std::to_wstring(m_recoveredFiles.size()) + L" files found.").c_str());
                }
                
                SendDlgItemMessage(hWnd, IDC_PROG_REC, PBM_SETPOS, 0, 0);
                EnableWindow(GetDlgItem(hWnd, IDC_BTN_SCAN), TRUE);
                EnableWindow(GetDlgItem(hWnd, IDC_BTN_RECOVER), TRUE);
                ShowWindow(GetDlgItem(hWnd, IDC_BTN_REC_PAUSE), SW_HIDE);
                ShowWindow(GetDlgItem(hWnd, IDC_BTN_REC_STOP), SW_HIDE);
            }).detach();
        } else if (wmId == IDC_BTN_REC_PAUSE) {
            m_pauseRecovery = !m_pauseRecovery;
            SetDlgItemTextW(hWnd, IDC_BTN_REC_PAUSE, m_pauseRecovery ? L"계속" : L"일시정지");
        } else if (wmId == IDC_BTN_REC_STOP) {
            m_cancelRecovery = true;
            m_pauseRecovery = false; // resume so it can exit
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
    SendMessageW(hTree, WM_SETREDRAW, FALSE, 0);
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
    
    SendMessageW(hTree, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hTree, NULL, TRUE);
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

namespace {
    bool RunCommandHidden(const std::wstring& cmd, DWORD& exitCode) {
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = { 0 };
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        std::wstring cmdMutable = cmd;
        if (CreateProcessW(NULL, &cmdMutable[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            GetExitCodeProcess(pi.hProcess, &exitCode);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return true;
        }
        return false;
    }
}

bool MainWindow::IsAutoStartEnabled() {
    std::wstring cmd = L"schtasks /Query /TN \"clearMax_AutoStart\"";
    DWORD exitCode = 0;
    if (RunCommandHidden(cmd, exitCode)) {
        return exitCode == 0;
    }
    return false;
}

void MainWindow::ToggleAutoStart(bool enable) {
    if (!enable) {
        if (IsAutoStartEnabled()) {
            std::wstring cmd = L"schtasks /Delete /TN \"clearMax_AutoStart\" /F";
            DWORD exitCode = 0;
            RunCommandHidden(cmd, exitCode);
        }
    } else {
        if (!IsAutoStartEnabled()) {
            wchar_t path[MAX_PATH];
            GetModuleFileNameW(NULL, path, MAX_PATH);
            std::wstring exePath = path;
            std::wstring cmd = L"schtasks /Create /TN \"clearMax_AutoStart\" /TR \"\\\"" + exePath + L"\\\" /autostart\" /SC ONLOGON /RL HIGHEST /F";
            DWORD exitCode = 0;
            RunCommandHidden(cmd, exitCode);
        }
    }
}

// -----------------------------------------------------------------------------
// Settings Tab
// -----------------------------------------------------------------------------
INT_PTR CALLBACK MainWindow::SettingsDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    MainWindow* pThis = nullptr;
    if (message == WM_INITDIALOG) {
        pThis = (MainWindow*)lParam;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
        
        if (IsAutoStartEnabled()) {
            CheckDlgButton(hWnd, IDC_CHK_AUTOSTART, BST_CHECKED);
        } else {
            CheckDlgButton(hWnd, IDC_CHK_AUTOSTART, BST_UNCHECKED);
        }
        
        return (INT_PTR)TRUE;
    } else {
        pThis = (MainWindow*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    }

    if (pThis) return pThis->HandleSettingsMessage(hWnd, message, wParam, lParam);
    return (INT_PTR)FALSE;
}

INT_PTR MainWindow::HandleSettingsMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_COMMAND) {
        int wmId = LOWORD(wParam);
        if (wmId == IDC_CHK_AUTOSTART) {
            bool enable = (IsDlgButtonChecked(hWnd, IDC_CHK_AUTOSTART) == BST_CHECKED);
            ToggleAutoStart(enable);
        }
    }
    return (INT_PTR)FALSE;
}

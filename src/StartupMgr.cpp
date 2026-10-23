#include "StartupMgr.h"
#include <Shlwapi.h>
#include <Shlobj.h>
#include <shobjidl.h>
#include <filesystem>
#include <taskschd.h>
#include <sstream>
#include <algorithm>
#include <cctype>

#pragma comment(lib, "taskschd.lib")

namespace fs = std::filesystem;

static void ScanRegistryRunKey(HKEY root, const std::wstring& subKey, const std::wstring& locationName, std::vector<StartupItem>& items, bool isEnabled) {
    HKEY hKey;
    if (RegOpenKeyExW(root, subKey.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD valuesCount = 0;
        DWORD maxValueNameLen = 0;
        DWORD maxValueLen = 0;
        RegQueryInfoKeyW(hKey, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &valuesCount, &maxValueNameLen, &maxValueLen, nullptr, nullptr);

        std::vector<wchar_t> valueNameBuffer(maxValueNameLen + 1);
        std::vector<BYTE> valueBuffer(maxValueLen + 1);

        for (DWORD i = 0; i < valuesCount; ++i) {
            DWORD nameLen = maxValueNameLen + 1;
            DWORD dataLen = maxValueLen + 1;
            DWORD type = 0;
            if (RegEnumValueW(hKey, i, valueNameBuffer.data(), &nameLen, nullptr, &type, valueBuffer.data(), &dataLen) == ERROR_SUCCESS) {
                if (type == REG_SZ || type == REG_EXPAND_SZ) {
                    StartupItem item;
                    item.name = valueNameBuffer.data();
                    size_t strLen = dataLen / sizeof(wchar_t);
                    if (strLen > 0 && reinterpret_cast<wchar_t*>(valueBuffer.data())[strLen-1] == L'\0') strLen--;
                    item.command = std::wstring(reinterpret_cast<wchar_t*>(valueBuffer.data()), strLen);
                    item.location = locationName;
                    item.isEnabled = isEnabled;
                    item.regKeyPath = subKey;
                    item.regValueName = item.name;
                    item.regRoot = root;
                    item.isFolderShortcut = false;
                    items.push_back(item);
                }
            }
        }
        RegCloseKey(hKey);
    }
}

static void ScanStartupFolder(int csidl, const std::wstring& locationName, std::vector<StartupItem>& items) {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, csidl, NULL, 0, path))) {
        // 1. Scan Enabled (in actual Startup folder)
        std::wstring searchPath = std::wstring(path) + L"\\*";
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    std::wstring fileName = fd.cFileName;
                    if (fileName == L"desktop.ini") continue;
                    
                    bool isEnabled = true;
                    if (fileName.length() > 9 && fileName.substr(fileName.length() - 9) == L".disabled") {
                        isEnabled = false;
                        fileName = fileName.substr(0, fileName.length() - 9);
                    }
                    
                    StartupItem item;
                    item.name = fileName;
                    item.command = std::wstring(path) + L"\\" + fd.cFileName;
                    item.location = locationName;
                    item.isEnabled = isEnabled;
                    item.isFolderShortcut = true;
                    item.shortcutPath = item.command;
                    item.csidl = csidl;
                    items.push_back(item);
                }
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }

        // 2. Scan Disabled (in StartupDisabled folder)
        std::wstring disabledPath = std::wstring(path) + L"Disabled";
        searchPath = disabledPath + L"\\*";
        hFind = FindFirstFileW(searchPath.c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    std::wstring fileName = fd.cFileName;
                    if (fileName == L"desktop.ini") continue;
                    
                    StartupItem item;
                    item.name = fileName;
                    item.command = disabledPath + L"\\" + fd.cFileName;
                    item.location = locationName;
                    item.isEnabled = false;
                    item.isFolderShortcut = true;
                    item.shortcutPath = item.command;
                    item.csidl = csidl;
                    items.push_back(item);
                }
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }
    }
}

static void ProcessTaskFolder(ITaskFolder* pRootFolder, std::vector<StartupItem>& items) {
    IRegisteredTaskCollection* pTaskCollection = NULL;
    if (SUCCEEDED(pRootFolder->GetTasks(TASK_ENUM_HIDDEN, &pTaskCollection))) {
        LONG numTasks = 0;
        pTaskCollection->get_Count(&numTasks);
        for (LONG i = 1; i <= numTasks; i++) {
            IRegisteredTask* pTask = NULL;
            VARIANT varIdx;
            VariantInit(&varIdx);
            varIdx.vt = VT_I4;
            varIdx.lVal = i;
            if (SUCCEEDED(pTaskCollection->get_Item(varIdx, &pTask))) {
                bool isStartup = false;
                std::wstring command = L"";

                ITaskDefinition* pDefinition = NULL;
                if (SUCCEEDED(pTask->get_Definition(&pDefinition))) {
                    ITriggerCollection* pTriggers = NULL;
                    if (SUCCEEDED(pDefinition->get_Triggers(&pTriggers))) {
                        LONG count = 0;
                        pTriggers->get_Count(&count);
                        for (LONG t = 1; t <= count; t++) {
                            ITrigger* pTrigger = NULL;
                            if (SUCCEEDED(pTriggers->get_Item(t, &pTrigger))) {
                                TASK_TRIGGER_TYPE2 type;
                                if (SUCCEEDED(pTrigger->get_Type(&type))) {
                                    if (type == TASK_TRIGGER_LOGON || type == TASK_TRIGGER_BOOT) {
                                        isStartup = true;
                                    }
                                }
                                pTrigger->Release();
                            }
                        }
                        pTriggers->Release();
                    }

                    IActionCollection* pActions = NULL;
                    if (SUCCEEDED(pDefinition->get_Actions(&pActions))) {
                        IAction* pAction = NULL;
                        if (SUCCEEDED(pActions->get_Item(1, &pAction))) {
                            TASK_ACTION_TYPE actionType;
                            if (SUCCEEDED(pAction->get_Type(&actionType)) && actionType == TASK_ACTION_EXEC) {
                                IExecAction* pExecAction = NULL;
                                if (SUCCEEDED(pAction->QueryInterface(IID_IExecAction, (void**)&pExecAction))) {
                                    BSTR bstrExec = NULL;
                                    BSTR bstrArgs = NULL;
                                    pExecAction->get_Path(&bstrExec);
                                    pExecAction->get_Arguments(&bstrArgs);
                                    
                                    if (bstrExec) command = bstrExec;
                                    if (bstrArgs) {
                                        command += L" ";
                                        command += bstrArgs;
                                    }
                                    
                                    if (bstrExec) SysFreeString(bstrExec);
                                    if (bstrArgs) SysFreeString(bstrArgs);
                                    pExecAction->Release();
                                }
                            }
                            pAction->Release();
                        }
                        pActions->Release();
                    }
                    pDefinition->Release();
                }

                if (isStartup) {
                    BSTR bstrName = NULL;
                    BSTR bstrPath = NULL;
                    pTask->get_Name(&bstrName);
                    pTask->get_Path(&bstrPath);

                    TASK_STATE state;
                    pTask->get_State(&state);
                    bool isEnabled = (state != TASK_STATE_DISABLED);

                    if (bstrName && bstrPath) {
                        StartupItem item;
                        item.name = bstrName;
                        item.command = command;
                        item.location = L"예약 작업 (Task Scheduler)";
                        item.isEnabled = isEnabled;
                        item.isFolderShortcut = false;
                        item.isScheduledTask = true;
                        item.taskPath = bstrPath;
                        items.push_back(item);
                    }
                    if (bstrName) SysFreeString(bstrName);
                    if (bstrPath) SysFreeString(bstrPath);
                }
                pTask->Release();
            }
        }
        pTaskCollection->Release();
    }

    ITaskFolderCollection* pSubFolders = NULL;
    if (SUCCEEDED(pRootFolder->GetFolders(0, &pSubFolders))) {
        LONG numFolders = 0;
        pSubFolders->get_Count(&numFolders);
        for (LONG i = 1; i <= numFolders; i++) {
            ITaskFolder* pSubFolder = NULL;
            VARIANT varIdx;
            VariantInit(&varIdx);
            varIdx.vt = VT_I4;
            varIdx.lVal = i;
            if (SUCCEEDED(pSubFolders->get_Item(varIdx, &pSubFolder))) {
                ProcessTaskFolder(pSubFolder, items);
                pSubFolder->Release();
            }
        }
        pSubFolders->Release();
    }
}

static void ScanScheduledTasks(std::vector<StartupItem>& items) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool coInit = SUCCEEDED(hr);

    ITaskService* pService = NULL;
    hr = CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, IID_ITaskService, (void**)&pService);
    if (SUCCEEDED(hr)) {
        VARIANT varEmpty;
        VariantInit(&varEmpty);
        hr = pService->Connect(varEmpty, varEmpty, varEmpty, varEmpty);
        if (SUCCEEDED(hr)) {
            ITaskFolder* pRootFolder = NULL;
            BSTR bstrRoot = SysAllocString(L"\\");
            hr = pService->GetFolder(bstrRoot, &pRootFolder);
            SysFreeString(bstrRoot);
            if (SUCCEEDED(hr)) {
                ProcessTaskFolder(pRootFolder, items);
                pRootFolder->Release();
            }
        }
        pService->Release();
    }
    if (coInit) {
        CoUninitialize();
    }
}

std::vector<StartupItem> StartupMgr::getStartupItems() {
    std::vector<StartupItem> items;
    
    // Active
    ScanRegistryRunKey(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", L"HKLM\\...\\Run", items, true);
    ScanRegistryRunKey(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run", L"HKLM\\...\\Run (Wow64)", items, true);
    ScanRegistryRunKey(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", L"HKCU\\...\\Run", items, true);
    
    // Disabled
    ScanRegistryRunKey(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run-", L"HKLM\\...\\Run-", items, false);
    ScanRegistryRunKey(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run-", L"HKLM\\...\\Run- (Wow64)", items, false);
    ScanRegistryRunKey(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run-", L"HKCU\\...\\Run-", items, false);

    // Startup Folders
    ScanStartupFolder(CSIDL_COMMON_STARTUP, L"Common Startup Folder", items);
    ScanStartupFolder(CSIDL_STARTUP, L"User Startup Folder", items);
    
    // Scheduled Tasks
    ScanScheduledTasks(items);

    // Validate item files existence
    for (auto& item : items) {
        item.isFileMissing = !isItemValid(item);
    }

    return items;
}

bool StartupMgr::setStartupItemState(StartupItem& item, bool enable) {
    if (item.isEnabled == enable) return true;

    if (item.isScheduledTask) {
        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        bool coInit = SUCCEEDED(hr);
        bool success = false;
        ITaskService* pService = NULL;
        if (SUCCEEDED(CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, IID_ITaskService, (void**)&pService))) {
            VARIANT varEmpty;
            VariantInit(&varEmpty);
            if (SUCCEEDED(pService->Connect(varEmpty, varEmpty, varEmpty, varEmpty))) {
                ITaskFolder* pRootFolder = NULL;
                BSTR bstrRoot = SysAllocString(L"\\");
                if (SUCCEEDED(pService->GetFolder(bstrRoot, &pRootFolder))) {
                    IRegisteredTask* pTask = NULL;
                    BSTR bstrPath = SysAllocString(item.taskPath.c_str());
                    if (SUCCEEDED(pRootFolder->GetTask(bstrPath, &pTask))) {
                        if (SUCCEEDED(pTask->put_Enabled(enable ? VARIANT_TRUE : VARIANT_FALSE))) {
                            success = true;
                            item.isEnabled = enable;
                        }
                        pTask->Release();
                    }
                    SysFreeString(bstrPath);
                    pRootFolder->Release();
                }
                SysFreeString(bstrRoot);
            }
            pService->Release();
        }
        if (coInit) CoUninitialize();
        return success;
    }

    if (item.isFolderShortcut) {
        wchar_t rootPath[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, item.csidl, NULL, 0, rootPath))) {
            std::wstring enabledDir = rootPath;
            std::wstring disabledDir = enabledDir + L"Disabled";
            
            // Ensure Disabled directory exists
            if (!enable) {
                CreateDirectoryW(disabledDir.c_str(), NULL);
            }
            
            std::wstring fileName = fs::path(item.shortcutPath).filename().wstring();
            
            // Handle old .disabled suffix if present
            if (enable && fileName.length() > 9 && fileName.substr(fileName.length() - 9) == L".disabled") {
                fileName = fileName.substr(0, fileName.length() - 9);
            }
            
            std::wstring newPath = (enable ? enabledDir : disabledDir) + L"\\" + fileName;
            
            if (MoveFileW(item.shortcutPath.c_str(), newPath.c_str())) {
                item.shortcutPath = newPath;
                item.isEnabled = enable;
                item.name = fileName;
                return true;
            }
        }
        return false;
    } else {
        std::wstring newSubKey = item.regKeyPath;
        if (enable) {
            if (newSubKey.back() == L'-') newSubKey.pop_back();
        } else {
            if (newSubKey.back() != L'-') newSubKey += L"-";
        }
        
        HKEY hNewKey;
        if (RegCreateKeyExW(item.regRoot, newSubKey.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hNewKey, nullptr) == ERROR_SUCCESS) {
            if (RegSetValueExW(hNewKey, item.regValueName.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(item.command.c_str()), (item.command.length() + 1) * sizeof(wchar_t)) == ERROR_SUCCESS) {
                // Delete old
                HKEY hOldKey;
                if (RegOpenKeyExW(item.regRoot, item.regKeyPath.c_str(), 0, KEY_WRITE, &hOldKey) == ERROR_SUCCESS) {
                    RegDeleteValueW(hOldKey, item.regValueName.c_str());
                    RegCloseKey(hOldKey);
                }
                RegCloseKey(hNewKey);
                
                item.regKeyPath = newSubKey;
                if (enable) {
                    if (item.location.back() == L'-') item.location.pop_back();
                    else if (item.location.find(L"- ") != std::wstring::npos) {
                        item.location.erase(item.location.find(L"- "), 1);
                    }
                } else {
                    size_t pos = item.location.find(L" (");
                    if (pos != std::wstring::npos) {
                        item.location.insert(pos, L"-");
                    } else {
                        item.location += L"-";
                    }
                }
                item.isEnabled = enable;
                return true;
            }
            RegCloseKey(hNewKey);
        }
    }
    return false;
}

bool StartupMgr::deleteStartupItem(const StartupItem& item) {
    if (item.isScheduledTask) {
        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        bool coInit = SUCCEEDED(hr);
        bool success = false;
        ITaskService* pService = NULL;
        if (SUCCEEDED(CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, IID_ITaskService, (void**)&pService))) {
            VARIANT varEmpty;
            VariantInit(&varEmpty);
            if (SUCCEEDED(pService->Connect(varEmpty, varEmpty, varEmpty, varEmpty))) {
                ITaskFolder* pRootFolder = NULL;
                BSTR bstrRoot = SysAllocString(L"\\");
                if (SUCCEEDED(pService->GetFolder(bstrRoot, &pRootFolder))) {
                    BSTR bstrPath = SysAllocString(item.taskPath.c_str());
                    if (SUCCEEDED(pRootFolder->DeleteTask(bstrPath, 0))) {
                        success = true;
                    }
                    SysFreeString(bstrPath);
                    pRootFolder->Release();
                }
                SysFreeString(bstrRoot);
            }
            pService->Release();
        }
        if (coInit) CoUninitialize();
        return success;
    }

    if (item.isFolderShortcut) {
        return DeleteFileW(item.shortcutPath.c_str()) != 0;
    } else {
        HKEY hKey;
        if (RegOpenKeyExW(item.regRoot, item.regKeyPath.c_str(), 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
            LSTATUS res = RegDeleteValueW(hKey, item.regValueName.c_str());
            RegCloseKey(hKey);
            return res == ERROR_SUCCESS;
        }
    }
    return false;
}

std::wstring StartupMgr::resolveShortcutTarget(const std::wstring& lnkPath) {
    std::wstring targetPath = L"";
    HRESULT hrCo = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool coInit = SUCCEEDED(hrCo);

    IShellLinkW* psl = NULL;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&psl);
    if (SUCCEEDED(hr)) {
        IPersistFile* ppf = NULL;
        hr = psl->QueryInterface(IID_IPersistFile, (void**)&ppf);
        if (SUCCEEDED(hr)) {
            hr = ppf->Load(lnkPath.c_str(), STGM_READ);
            if (SUCCEEDED(hr)) {
                wchar_t szGotPath[MAX_PATH] = { 0 };
                WIN32_FIND_DATAW wfd;
                hr = psl->GetPath(szGotPath, MAX_PATH, &wfd, SLGP_UNCPRIORITY);
                if (SUCCEEDED(hr)) {
                    targetPath = szGotPath;
                }
            }
            ppf->Release();
        }
        psl->Release();
    }
    if (coInit) CoUninitialize();
    return targetPath;
}

static bool CheckFileExistsHelper(const std::wstring& rawCommand) {
    if (rawCommand.empty()) return false;

    // 1. 환경 변수 확장
    wchar_t expanded[MAX_PATH * 4] = { 0 };
    DWORD expLen = ExpandEnvironmentStringsW(rawCommand.c_str(), expanded, ARRAYSIZE(expanded));
    std::wstring cmd = (expLen > 0) ? expanded : rawCommand;

    // 앞뒤 공백 제거
    while (!cmd.empty() && iswspace(cmd.front())) cmd.erase(cmd.begin());
    while (!cmd.empty() && iswspace(cmd.back())) cmd.pop_back();
    if (cmd.empty()) return false;

    // 2. rundll32.exe 호출 처리
    std::wstring cmdLower = cmd;
    std::transform(cmdLower.begin(), cmdLower.end(), cmdLower.begin(), ::towlower);
    if (cmdLower.rfind(L"rundll32", 0) == 0) {
        size_t firstSpace = cmd.find(L' ');
        if (firstSpace != std::wstring::npos) {
            std::wstring args = cmd.substr(firstSpace + 1);
            while (!args.empty() && iswspace(args.front())) args.erase(args.begin());

            std::wstring dllPath = L"";
            if (!args.empty() && args.front() == L'\"') {
                size_t closeQ = args.find(L'\"', 1);
                if (closeQ != std::wstring::npos) {
                    dllPath = args.substr(1, closeQ - 1);
                }
            } else {
                size_t comma = args.find(L',');
                size_t space = args.find(L' ');
                size_t endPos = (comma != std::wstring::npos) ? comma : space;
                if (endPos != std::wstring::npos) {
                    dllPath = args.substr(0, endPos);
                } else {
                    dllPath = args;
                }
            }

            if (!dllPath.empty()) {
                wchar_t dllExp[MAX_PATH * 2] = { 0 };
                ExpandEnvironmentStringsW(dllPath.c_str(), dllExp, ARRAYSIZE(dllExp));
                if (fs::exists(dllExp) && !fs::is_directory(dllExp)) {
                    return true;
                }
                wchar_t found[MAX_PATH] = { 0 };
                if (SearchPathW(NULL, dllExp, L".dll", MAX_PATH, found, NULL) > 0) {
                    return true;
                }
                return false;
            }
        }
    }

    // 3. 따옴표로 감싸진 경로
    if (cmd.front() == L'\"') {
        size_t closeQuote = cmd.find(L'\"', 1);
        if (closeQuote != std::wstring::npos) {
            std::wstring exePath = cmd.substr(1, closeQuote - 1);
            if (fs::exists(exePath) && !fs::is_directory(exePath)) {
                return true;
            }
            wchar_t found[MAX_PATH] = { 0 };
            if (SearchPathW(NULL, exePath.c_str(), NULL, MAX_PATH, found, NULL) > 0) {
                return true;
            }
            return false;
        }
    }

    // 4. 따옴표 없는 경로 (공백 포함 경로 탐색)
    std::wstring candidate = L"";
    std::wistringstream iss(cmd);
    std::wstring token;
    while (iss >> token) {
        if (!candidate.empty()) candidate += L" ";
        candidate += token;

        if (fs::exists(candidate) && !fs::is_directory(candidate)) {
            return true;
        }
    }

    // 5. 첫 번째 토큰만으로 SearchPath 검색 (시스템 유틸리티 등)
    size_t firstSpace = cmd.find(L' ');
    std::wstring firstToken = (firstSpace != std::wstring::npos) ? cmd.substr(0, firstSpace) : cmd;
    wchar_t found[MAX_PATH] = { 0 };
    if (SearchPathW(NULL, firstToken.c_str(), L".exe", MAX_PATH, found, NULL) > 0) {
        return true;
    }

    return false;
}

bool StartupMgr::isItemValid(const StartupItem& item) {
    // 1. 단축아이콘(.lnk)인 경우
    if (item.isFolderShortcut) {
        if (!fs::exists(item.shortcutPath)) {
            return false;
        }
        std::wstring ext = fs::path(item.shortcutPath).extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        if (ext == L".lnk") {
            std::wstring targetPath = resolveShortcutTarget(item.shortcutPath);
            if (!targetPath.empty()) {
                if (targetPath.find(L":\\") != std::wstring::npos || targetPath.rfind(L"\\\\", 0) == 0) {
                    wchar_t expanded[MAX_PATH * 2] = { 0 };
                    ExpandEnvironmentStringsW(targetPath.c_str(), expanded, ARRAYSIZE(expanded));
                    return fs::exists(expanded);
                }
            }
        }
        return true;
    }

    // 2. 예약 작업 (Task Scheduler)
    if (item.isScheduledTask) {
        // Microsoft Windows 내장 기본 작업은 안전을 위해 유효한 것으로 간주(보호)
        if (item.taskPath.rfind(L"\\Microsoft\\Windows\\", 0) == 0 ||
            item.taskPath.rfind(L"\\Microsoft\\", 0) == 0) {
            return true;
        }
        if (item.command.empty()) return false;
        return CheckFileExistsHelper(item.command);
    }

    // 3. 레지스트리 항목
    if (item.command.empty()) return false;
    return CheckFileExistsHelper(item.command);
}

std::vector<StartupItem> StartupMgr::getInvalidStartupItems() {
    auto allItems = getStartupItems();
    std::vector<StartupItem> invalidItems;
    for (const auto& item : allItems) {
        if (!isItemValid(item)) {
            invalidItems.push_back(item);
        }
    }
    return invalidItems;
}

int StartupMgr::cleanInvalidStartupItems(std::function<void(int)> progressCallback) {
    auto invalidItems = getInvalidStartupItems();
    int cleanedCount = 0;
    size_t total = invalidItems.size();
    if (total == 0) {
        if (progressCallback) progressCallback(100);
        return 0;
    }

    for (size_t i = 0; i < total; ++i) {
        if (deleteStartupItem(invalidItems[i])) {
            cleanedCount++;
        }
        if (progressCallback) {
            progressCallback((int)((i + 1) * 100 / total));
        }
    }
    return cleanedCount;
}

int StartupMgr::removeStartupItemsForProgram(const std::wstring& installDir, const std::wstring& displayName) {
    auto allItems = getStartupItems();
    int removedCount = 0;

    std::wstring lowerDir = installDir;
    std::transform(lowerDir.begin(), lowerDir.end(), lowerDir.begin(), ::towlower);
    while (!lowerDir.empty() && (lowerDir.back() == L'\\' || lowerDir.back() == L'/')) {
        lowerDir.pop_back();
    }

    std::wstring lowerName = displayName;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::towlower);

    for (const auto& item : allItems) {
        bool match = false;

        // 1. 커맨드라인 경로 매칭
        if (!lowerDir.empty()) {
            std::wstring lowerCmd = item.command;
            std::transform(lowerCmd.begin(), lowerCmd.end(), lowerCmd.begin(), ::towlower);
            if (lowerCmd.find(lowerDir) != std::wstring::npos) {
                match = true;
            }
        }

        // 2. 단축아이콘(.lnk)인 경우 실제 타깃 경로 매칭
        if (!match && item.isFolderShortcut && !lowerDir.empty()) {
            std::wstring target = resolveShortcutTarget(item.shortcutPath);
            std::transform(target.begin(), target.end(), target.begin(), ::towlower);
            if (target.find(lowerDir) != std::wstring::npos) {
                match = true;
            }
        }

        // 3. 프로그램 이름(DisplayName)과의 매칭 (3글자 이상)
        if (!match && lowerName.length() >= 3) {
            std::wstring lowerItemName = item.name;
            std::transform(lowerItemName.begin(), lowerItemName.end(), lowerItemName.begin(), ::towlower);
            if (lowerItemName.find(lowerName) != std::wstring::npos || lowerName.find(lowerItemName) != std::wstring::npos) {
                // Windows 핵심 예약 작업 보호
                if (!item.isScheduledTask || 
                    (item.taskPath.rfind(L"\\Microsoft\\Windows\\", 0) != 0 && item.taskPath.rfind(L"\\Microsoft\\", 0) != 0)) {
                    match = true;
                }
            }
        }

        // 4. 소멸된 유령 시작프로그램 추가 검증
        if (!match && !lowerDir.empty() && !isItemValid(item)) {
            std::wstring lowerCmd = item.command;
            std::transform(lowerCmd.begin(), lowerCmd.end(), lowerCmd.begin(), ::towlower);
            if (lowerCmd.find(lowerDir) != std::wstring::npos) {
                match = true;
            }
        }

        if (match) {
            if (deleteStartupItem(item)) {
                removedCount++;
            }
        }
    }

    return removedCount;
}

#include "StartupMgr.h"
#include <Shlwapi.h>
#include <Shlobj.h>
#include <filesystem>
#include <taskschd.h>

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
        std::wstring searchPath = std::wstring(path) + L"\\*";
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    std::wstring fileName = fd.cFileName;
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
        std::wstring newPath = item.shortcutPath;
        if (enable) {
            if (newPath.length() > 9 && newPath.substr(newPath.length() - 9) == L".disabled") {
                newPath = newPath.substr(0, newPath.length() - 9);
            }
        } else {
            newPath += L".disabled";
        }
        if (MoveFileW(item.shortcutPath.c_str(), newPath.c_str())) {
            item.shortcutPath = newPath;
            item.isEnabled = enable;
            return true;
        }
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

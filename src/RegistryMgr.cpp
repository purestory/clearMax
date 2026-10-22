#include "RegistryMgr.h"
#include "FileShredder.h"
#include "StartupMgr.h"
#include <filesystem>
#include <thread>
#include <fstream>
#include <Shlwapi.h>
#include <algorithm>
#include <TlHelp32.h>
#include <aclapi.h>
#include <RestartManager.h>
#pragma comment(lib, "RstrtMgr.lib")
#pragma comment(lib, "Advapi32.lib")
namespace fs = std::filesystem;

std::wstring RegistryMgr::getRegString(HKEY key, const std::wstring& valueName) {
    DWORD bufferSize = 0;
    
    if (RegQueryValueExW(key, valueName.c_str(), nullptr, nullptr, nullptr, &bufferSize) == ERROR_SUCCESS) {
        std::vector<wchar_t> buffer(bufferSize / sizeof(wchar_t));
        if (RegQueryValueExW(key, valueName.c_str(), nullptr, nullptr, reinterpret_cast<LPBYTE>(buffer.data()), &bufferSize) == ERROR_SUCCESS) {
            // RegQueryValueExW might return string with null terminator, we don't want it in std::wstring length
            size_t len = bufferSize / sizeof(wchar_t);
            if (len > 0 && buffer[len-1] == L'\0') {
                len--;
            }
            return std::wstring(buffer.data(), len);
        }
    }
    return std::wstring();
}

DWORD RegistryMgr::getRegDword(HKEY key, const std::wstring& valueName) {
    DWORD bufferSize = sizeof(DWORD);
    DWORD value = 0;
    
    if (RegQueryValueExW(key, valueName.c_str(), nullptr, nullptr, reinterpret_cast<LPBYTE>(&value), &bufferSize) == ERROR_SUCCESS) {
        return value;
    }
    return 0;
}

void RegistryMgr::scanRegistryKey(HKEY rootKey, const std::wstring& subKeyPath, std::vector<ProgramInfo>& programs) {
    HKEY hKey;
    
    if (RegOpenKeyExW(rootKey, subKeyPath.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &hKey) != ERROR_SUCCESS) {
        if (RegOpenKeyExW(rootKey, subKeyPath.c_str(), 0, KEY_READ | KEY_WOW64_32KEY, &hKey) != ERROR_SUCCESS) {
            return;
        }
    }

    DWORD subKeysCount = 0;
    DWORD maxSubKeyLen = 0;
    RegQueryInfoKeyW(hKey, nullptr, nullptr, nullptr, &subKeysCount, &maxSubKeyLen, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

    std::vector<wchar_t> keyNameBuffer(maxSubKeyLen + 1);

    for (DWORD i = 0; i < subKeysCount; ++i) {
        DWORD keyNameLen = maxSubKeyLen + 1;
        FILETIME ftLastWriteTime;
        if (RegEnumKeyExW(hKey, i, keyNameBuffer.data(), &keyNameLen, nullptr, nullptr, nullptr, &ftLastWriteTime) == ERROR_SUCCESS) {
            std::wstring appKeyName = keyNameBuffer.data();
            std::wstring fullAppKeyPath = subKeyPath + L"\\" + appKeyName;
            
            HKEY hAppKey;
            if (RegOpenKeyExW(rootKey, fullAppKeyPath.c_str(), 0, KEY_READ, &hAppKey) == ERROR_SUCCESS) {
                std::wstring displayName = getRegString(hAppKey, L"DisplayName");
                std::wstring uninstallString = getRegString(hAppKey, L"UninstallString");
                
                // Fallback 1: QuietUninstallString
                if (uninstallString.empty()) {
                    uninstallString = getRegString(hAppKey, L"QuietUninstallString");
                }
                
                // Fallback 2: MSI GUIDs implicitly use msiexec /X
                if (uninstallString.empty() && appKeyName.front() == L'{' && appKeyName.back() == L'}') {
                    uninstallString = L"msiexec.exe /X" + appKeyName;
                }
                
                DWORD systemComponentDw = getRegDword(hAppKey, L"SystemComponent");
                std::wstring systemComponentStr = getRegString(hAppKey, L"SystemComponent");

                // Filter out system components and empty names
                if (!displayName.empty() && systemComponentDw != 1 && systemComponentStr != L"1") {
                    ProgramInfo info;
                    info.displayName = displayName;
                    info.displayVersion = getRegString(hAppKey, L"DisplayVersion");
                    info.publisher = getRegString(hAppKey, L"Publisher");
                    info.uninstallString = uninstallString;
                    info.registryKeyPath = fullAppKeyPath;
                    info.displayIcon = getRegString(hAppKey, L"DisplayIcon");
                    
                    info.installDate = getRegString(hAppKey, L"InstallDate");
                    if (info.installDate.empty()) {
                        SYSTEMTIME stUTC, stLocal;
                        if (FileTimeToSystemTime(&ftLastWriteTime, &stUTC) && SystemTimeToTzSpecificLocalTime(NULL, &stUTC, &stLocal)) {
                            wchar_t buf[16];
                            swprintf(buf, 16, L"%04d%02d%02d", stLocal.wYear, stLocal.wMonth, stLocal.wDay);
                            info.installDate = buf;
                        }
                    }
                    
                    info.estimatedSize = getRegDword(hAppKey, L"EstimatedSize");
                    
                    auto checkPathExists = [](const std::wstring& path) -> bool {
                        if (path.empty()) return false;
                        wchar_t expanded[MAX_PATH];
                        ExpandEnvironmentStringsW(path.c_str(), expanded, MAX_PATH);
                        std::error_code ec;
                        return fs::exists(expanded, ec);
                    };

                    // Improved ghost detection
                    std::wstring installLocation = getRegString(hAppKey, L"InstallLocation");
                    if (!installLocation.empty()) {
                        installLocation.erase(std::remove(installLocation.begin(), installLocation.end(), L'\"'), installLocation.end());
                        info.isGhost = !checkPathExists(installLocation);
                    } else if (!uninstallString.empty()) {
                        std::wstring exePath = uninstallString;
                        if (exePath.front() == L'\"') {
                            size_t endQuote = exePath.find(L'\"', 1);
                            if (endQuote != std::wstring::npos) {
                                exePath = exePath.substr(1, endQuote - 1);
                            }
                        } else {
                            std::wstring lowerExePath = exePath;
                            std::transform(lowerExePath.begin(), lowerExePath.end(), lowerExePath.begin(), ::towlower);
                            size_t exeIdx = lowerExePath.find(L".exe");
                            if (exeIdx != std::wstring::npos) {
                                exePath = exePath.substr(0, exeIdx + 4);
                            }
                        }
                        
                        std::wstring lowerExe = exePath;
                        std::transform(lowerExe.begin(), lowerExe.end(), lowerExe.begin(), ::towlower);
                        
                        // If it's a system executable without a path, assume it exists
                        if (lowerExe == L"msiexec.exe" || lowerExe == L"rundll32.exe" || exePath.find(L"\\") == std::wstring::npos) {
                            info.isGhost = false;
                        } else if (lowerExe.find(L"package cache") != std::wstring::npos) {
                            info.isGhost = false; // Trust that Package Cache managed apps are not ghosts even if cache is cleared
                        } else {
                            info.isGhost = !checkPathExists(exePath);
                        }
                    } else {
                        info.isGhost = false; // Not a ghost; likely a bundled component or update without a dedicated uninstaller.
                    }
                    
                    info.installLocation = installLocation;
                    
                    info.lastUsedDate = L""; // Fetched asynchronously later
                    
                    programs.push_back(info);
                }
                RegCloseKey(hAppKey);
            }
        }
    }
    RegCloseKey(hKey);
}

std::vector<ProgramInfo> RegistryMgr::getInstalledPrograms() {
    std::vector<ProgramInfo> programs;
    
    std::wstring uninstallPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
    std::wstring uninstallPathWow64 = L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall";

    // Scan HKLM
    scanRegistryKey(HKEY_LOCAL_MACHINE, uninstallPath, programs);
    scanRegistryKey(HKEY_LOCAL_MACHINE, uninstallPathWow64, programs);
    
    // Scan HKCU
    scanRegistryKey(HKEY_CURRENT_USER, uninstallPath, programs);

    // Remove duplicates based on displayName
    std::vector<ProgramInfo> uniquePrograms;
    for (const auto& p : programs) {
        bool exists = false;
        for (const auto& up : uniquePrograms) {
            if (up.displayName == p.displayName) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            uniquePrograms.push_back(p);
        }
    }

    return uniquePrograms;
}

bool RegistryMgr::uninstallProgram(const ProgramInfo& info, std::function<void()> onFinished) {
    if (info.uninstallString.empty()) return false;
    
    std::wstring cmdW = info.uninstallString;
    
    std::vector<wchar_t> cmdBuffer(cmdW.begin(), cmdW.end());
    cmdBuffer.push_back(L'\0');

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    bool success = CreateProcessW(
        nullptr,                
        cmdBuffer.data(),       
        nullptr,                
        nullptr,                
        FALSE,                  
        0,                      
        nullptr,                
        nullptr,                
        &si,                    
        &pi                     
    );

    if (success) {
        CloseHandle(pi.hThread);
        
        std::thread waitThread([hProcess = pi.hProcess, onFinished]() {
            WaitForSingleObject(hProcess, INFINITE);
            CloseHandle(hProcess);
            if (onFinished) {
                onFinished();
            }
        });
        waitThread.detach();
    }
    return success;
}

std::wstring RegistryMgr::fetchLastUsedDate(const std::wstring& installLocation, const std::wstring& uninstallString) {
    std::wstring latestDate = L"";
    FILETIME latestTime = {0, 0};

    auto updateLatestTime = [&](const FILETIME& ft) {
        if (CompareFileTime(&ft, &latestTime) > 0) {
            latestTime = ft;
            SYSTEMTIME stUTC, stLocal;
            if (FileTimeToSystemTime(&ft, &stUTC) && SystemTimeToTzSpecificLocalTime(NULL, &stUTC, &stLocal)) {
                wchar_t buf[64];
                swprintf(buf, 64, L"%04d-%02d-%02d", stLocal.wYear, stLocal.wMonth, stLocal.wDay);
                latestDate = buf;
            }
        }
    };

    // 1. Try to find the exact executable name
    std::wstring exeName = L"";
    std::wstring unExe = uninstallString;
    if (unExe.front() == L'"') {
        size_t pos = unExe.find(L'"', 1);
        if (pos != std::wstring::npos) unExe = unExe.substr(1, pos - 1);
    } else {
        size_t exePos = unExe.find(L".exe");
        if (exePos != std::wstring::npos) unExe = unExe.substr(0, exePos + 4);
    }
    size_t lastSlash = unExe.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        exeName = unExe.substr(lastSlash + 1);
    }
    
    // 2. Check Prefetch folder for this executable
    if (!exeName.empty()) {
        std::wstring prefetchPath = L"C:\\Windows\\Prefetch\\";
        prefetchPath += exeName;
        prefetchPath += L"-*.pf";
        
        WIN32_FIND_DATAW findData;
        HANDLE hFind = FindFirstFileW(prefetchPath.c_str(), &findData);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                updateLatestTime(findData.ftLastWriteTime);
            } while (FindNextFileW(hFind, &findData));
            FindClose(hFind);
        }
    }

    // 3. If still empty, scan the installation directory for any .exe's Prefetch
    if (latestDate.empty() && !installLocation.empty()) {
        std::wstring searchPath = installLocation;
        if (searchPath.back() != L'\\') searchPath += L'\\';
        searchPath += L"*.exe";
        
        WIN32_FIND_DATAW findData;
        HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    std::wstring pExeName = findData.cFileName;
                    std::wstring pPrefetch = L"C:\\Windows\\Prefetch\\" + pExeName + L"-*.pf";
                    WIN32_FIND_DATAW pfData;
                    HANDLE hPfFind = FindFirstFileW(pPrefetch.c_str(), &pfData);
                    if (hPfFind != INVALID_HANDLE_VALUE) {
                        do {
                            updateLatestTime(pfData.ftLastWriteTime);
                        } while (FindNextFileW(hPfFind, &pfData));
                        FindClose(hPfFind);
                    }
                }
            } while (FindNextFileW(hFind, &findData));
            FindClose(hFind);
        }
    }

    // 4. Fallback: just use the install location directory's last write time if absolutely nothing found
    // This is better than nothing, it at least shows when it was installed or updated
    if (latestDate.empty() && !installLocation.empty()) {
        WIN32_FILE_ATTRIBUTE_DATA fileInfo;
        if (GetFileAttributesExW(installLocation.c_str(), GetFileExInfoStandard, &fileInfo)) {
            updateLatestTime(fileInfo.ftLastWriteTime);
        }
    }

    return latestDate;
}

static void EnableAllPrivileges() {
    HANDLE hToken = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        const wchar_t* privNames[] = {
            SE_TAKE_OWNERSHIP_NAME,
            SE_BACKUP_NAME,
            SE_RESTORE_NAME,
            SE_DEBUG_NAME,
            SE_SHUTDOWN_NAME
        };
        for (const wchar_t* privName : privNames) {
            LUID luid;
            if (LookupPrivilegeValueW(NULL, privName, &luid)) {
                TOKEN_PRIVILEGES tp;
                tp.PrivilegeCount = 1;
                tp.Privileges[0].Luid = luid;
                tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
                AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL);
            }
        }
        CloseHandle(hToken);
    }
}

static void NormalizeAttributesAndAcl(const std::wstring& path) {
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);

    SID_IDENTIFIER_AUTHORITY SIDAuthWorld = SECURITY_WORLD_SID_AUTHORITY;
    PSID pEveryoneSID = NULL;
    if (AllocateAndInitializeSid(&SIDAuthWorld, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &pEveryoneSID)) {
        EXPLICIT_ACCESSW ea;
        ZeroMemory(&ea, sizeof(EXPLICIT_ACCESSW));
        ea.grfAccessPermissions = GENERIC_ALL;
        ea.grfAccessMode = SET_ACCESS;
        ea.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
        ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
        ea.Trustee.ptstrName = (LPWSTR)pEveryoneSID;

        PACL pACL = NULL;
        if (SetEntriesInAclW(1, &ea, NULL, &pACL) == ERROR_SUCCESS) {
            SetNamedSecurityInfoW((LPWSTR)path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, pACL, NULL);
            LocalFree(pACL);
        }
        FreeSid(pEveryoneSID);
    }
}

static void KillProcessesAndHolders(const std::wstring& dir) {
    DWORD sessionHandle = 0;
    WCHAR sessionKey[CCH_RM_SESSION_KEY + 1] = { 0 };
    if (RmStartSession(&sessionHandle, 0, sessionKey) == ERROR_SUCCESS) {
        LPCWSTR rgsFileNames[] = { dir.c_str() };
        RmRegisterResources(sessionHandle, 1, rgsFileNames, 0, NULL, 0, NULL);
        RmShutdown(sessionHandle, RmForceShutdown, NULL);
        RmEndSession(sessionHandle);
    }

    std::wstring lowerDir = dir;
    std::transform(lowerDir.begin(), lowerDir.end(), lowerDir.begin(), ::towlower);
    if (lowerDir.back() != L'\\') lowerDir += L'\\';

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (pe.th32ProcessID <= 4) continue;
                HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (hProcess) {
                    wchar_t pathBuf[MAX_PATH];
                    DWORD pathLen = MAX_PATH;
                    if (QueryFullProcessImageNameW(hProcess, 0, pathBuf, &pathLen)) {
                        std::wstring procPath = pathBuf;
                        std::transform(procPath.begin(), procPath.end(), procPath.begin(), ::towlower);
                        if (procPath.find(lowerDir) == 0) {
                            TerminateProcess(hProcess, 0);
                        }
                    }
                    CloseHandle(hProcess);
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
}

static void DisableAndCollectAssociatedServices(const std::wstring& targetDir, std::vector<std::wstring>& outServices, bool& needSafeMode) {
    std::wstring lowerTarget = targetDir;
    std::transform(lowerTarget.begin(), lowerTarget.end(), lowerTarget.begin(), ::towlower);
    if (lowerTarget.back() != L'\\') lowerTarget += L'\\';

    HKEY hServicesKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services", 0, KEY_READ, &hServicesKey) == ERROR_SUCCESS) {
        DWORD subKeyCount = 0;
        DWORD maxSubKeyLen = 0;
        RegQueryInfoKeyW(hServicesKey, NULL, NULL, NULL, &subKeyCount, &maxSubKeyLen, NULL, NULL, NULL, NULL, NULL, NULL);

        std::vector<wchar_t> keyBuffer(maxSubKeyLen + 1);
        for (DWORD i = 0; i < subKeyCount; ++i) {
            DWORD keyLen = maxSubKeyLen + 1;
            if (RegEnumKeyExW(hServicesKey, i, keyBuffer.data(), &keyLen, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
                std::wstring serviceName = keyBuffer.data();
                HKEY hSvc;
                if (RegOpenKeyExW(hServicesKey, serviceName.c_str(), 0, KEY_READ | KEY_WRITE, &hSvc) == ERROR_SUCCESS) {
                    DWORD valType = 0;
                    DWORD valSize = 0;
                    if (RegQueryValueExW(hSvc, L"ImagePath", NULL, &valType, NULL, &valSize) == ERROR_SUCCESS && valSize > 0) {
                        std::vector<wchar_t> pathRaw(valSize / sizeof(wchar_t) + 1);
                        if (RegQueryValueExW(hSvc, L"ImagePath", NULL, &valType, (LPBYTE)pathRaw.data(), &valSize) == ERROR_SUCCESS) {
                            wchar_t expanded[MAX_PATH * 2] = { 0 };
                            ExpandEnvironmentStringsW(pathRaw.data(), expanded, MAX_PATH * 2);
                            std::wstring imagePath = expanded;
                            std::transform(imagePath.begin(), imagePath.end(), imagePath.begin(), ::towlower);
                            
                            if (!imagePath.empty() && imagePath.front() == L'"') {
                                imagePath = imagePath.substr(1);
                                size_t qpos = imagePath.find(L'"');
                                if (qpos != std::wstring::npos) imagePath = imagePath.substr(0, qpos);
                            }
                            if (imagePath.rfind(L"\\??\\", 0) == 0) {
                                imagePath = imagePath.substr(4);
                            }

                            if (imagePath.find(lowerTarget) == 0) {
                                outServices.push_back(serviceName);

                                DWORD disabledVal = 4;
                                LONG setRes = RegSetValueExW(hSvc, L"Start", 0, REG_DWORD, (const BYTE*)&disabledVal, sizeof(DWORD));
                                if (setRes != ERROR_SUCCESS) {
                                    needSafeMode = true;
                                }
                            }
                        }
                    }
                    RegCloseKey(hSvc);
                }
            }
        }
        RegCloseKey(hServicesKey);
    }
}

static void ForceDeleteDirectoryDeep(const std::wstring& dir, bool& rebootRequired, bool& needSafeMode) {
    NormalizeAttributesAndAcl(dir);

    std::wstring searchPath = dir;
    if (searchPath.back() != L'\\') searchPath += L'\\';
    searchPath += L"*";

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;

            std::wstring fullPath = dir;
            if (fullPath.back() != L'\\') fullPath += L'\\';
            fullPath += fd.cFileName;

            NormalizeAttributesAndAcl(fullPath);

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                ForceDeleteDirectoryDeep(fullPath, rebootRequired, needSafeMode);
                if (!RemoveDirectoryW(fullPath.c_str())) {
                    MoveFileExW(fullPath.c_str(), NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
                    rebootRequired = true;
                }
            } else {
                if (!DeleteFileW(fullPath.c_str())) {
                    std::wstring tmpRename = fullPath + L"._cm_del_" + std::to_wstring(GetTickCount64());
                    if (MoveFileExW(fullPath.c_str(), tmpRename.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                        MoveFileExW(tmpRename.c_str(), NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
                    } else {
                        MoveFileExW(fullPath.c_str(), NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
                        needSafeMode = true;
                    }
                    rebootRequired = true;
                }
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    if (!RemoveDirectoryW(dir.c_str())) {
        MoveFileExW(dir.c_str(), NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
        rebootRequired = true;
    }
}

static SERVICE_STATUS g_ServiceStatus = { 0 };
static SERVICE_STATUS_HANDLE g_hStatus = NULL;

static VOID WINAPI PurgeServiceCtrlHandler(DWORD dwControl) {
    if (dwControl == SERVICE_CONTROL_STOP || dwControl == SERVICE_CONTROL_SHUTDOWN) {
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        if (g_hStatus) SetServiceStatus(g_hStatus, &g_ServiceStatus);
    }
}

static VOID WINAPI PurgeServiceMain(DWORD argc, LPWSTR *argv) {
    g_hStatus = RegisterServiceCtrlHandlerW(L"ClearMaxPurgeSvc", PurgeServiceCtrlHandler);
    if (g_hStatus) {
        g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
        g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
        g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
        SetServiceStatus(g_hStatus, &g_ServiceStatus);
    }

    // 1단계: bcdedit safeboot 즉시 해제 (어떤 예외가 터져도 다음 부팅은 100% 정상 부팅 보장)
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    wchar_t bcdCmd[] = L"bcdedit.exe /deletevalue {current} safeboot";
    if (CreateProcessW(NULL, bcdCmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    // 2단계: 레지스트리에서 대상 디렉토리와 서비스 목록 읽기
    std::wstring targetDir = L"";
    std::vector<std::wstring> services;
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\clearMax\\SafePurge", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t buf[MAX_PATH * 2] = { 0 };
        DWORD cbData = sizeof(buf);
        if (RegQueryValueExW(hKey, L"TargetDir", NULL, NULL, (LPBYTE)buf, &cbData) == ERROR_SUCCESS) {
            targetDir = buf;
        }

        std::vector<wchar_t> multiBuf(8192, 0);
        DWORD cbMulti = (DWORD)(multiBuf.size() * sizeof(wchar_t));
        if (RegQueryValueExW(hKey, L"Services", NULL, NULL, (LPBYTE)multiBuf.data(), &cbMulti) == ERROR_SUCCESS) {
            const wchar_t* p = multiBuf.data();
            while (*p) {
                services.push_back(p);
                p += wcslen(p) + 1;
            }
        }
        RegCloseKey(hKey);
    }
    SHDeleteKeyW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\clearMax\\SafePurge");

    // 3단계: 대상 폴더 파일 속성 해제 및 완전 강제 삭제 (안전모드이므로 드라이버 점유 0%)
    if (!targetDir.empty() && targetDir.length() > 3) {
        wchar_t attribCmd[MAX_PATH * 2];
        swprintf_s(attribCmd, L"attrib.exe -r -s -h /s /d \"%s\\*\"", targetDir.c_str());
        ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
        if (CreateProcessW(NULL, attribCmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 5000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }

        std::error_code ec;
        fs::remove_all(targetDir, ec);

        if (fs::exists(targetDir, ec)) {
            wchar_t rdCmd[MAX_PATH * 2];
            swprintf_s(rdCmd, L"cmd.exe /c rd /s /q \"%s\"", targetDir.c_str());
            ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
            if (CreateProcessW(NULL, rdCmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                WaitForSingleObject(pi.hProcess, 5000);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            }
        }
    }

    // 3-1단계: 시작프로그램 연동 및 소멸된 유령 시작 항목 완전 청소
    StartupMgr::removeStartupItemsForProgram(targetDir);
    StartupMgr::cleanInvalidStartupItems();

    // 4단계: 연관 서비스 강제 삭제
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (hSCM) {
        for (const auto& svc : services) {
            SC_HANDLE hS = OpenServiceW(hSCM, svc.c_str(), SERVICE_ALL_ACCESS);
            if (hS) {
                SERVICE_STATUS sStatus;
                ControlService(hS, SERVICE_CONTROL_STOP, &sStatus);
                DeleteService(hS);
                CloseServiceHandle(hS);
            }
        }

        // 5단계: 자기 자신 서비스 삭제
        SC_HANDLE hSelf = OpenServiceW(hSCM, L"ClearMaxPurgeSvc", SERVICE_ALL_ACCESS);
        if (hSelf) {
            DeleteService(hSelf);
            CloseServiceHandle(hSelf);
        }
        CloseServiceHandle(hSCM);
    }

    // SafeBoot 화이트리스트 정리
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\SafeBoot\\Minimal\\ClearMaxPurgeSvc");
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\SafeBoot\\Network\\ClearMaxPurgeSvc");

    // 보조 safepurge.cmd 및 RunOnce 잔재 정리
    wchar_t programDataPath[MAX_PATH];
    if (GetEnvironmentVariableW(L"ProgramData", programDataPath, MAX_PATH) != 0) {
        std::wstring oldCmd = std::wstring(programDataPath) + L"\\clearMax\\safepurge.cmd";
        DeleteFileW(oldCmd.c_str());
    }
    HKEY hRunOnce;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce", 0, KEY_SET_VALUE, &hRunOnce) == ERROR_SUCCESS) {
        RegDeleteValueW(hRunOnce, L"*clearMaxSafePurge");
        RegCloseKey(hRunOnce);
    }

    // 6단계: 0초 만에 일반 정상 모드로 즉시 재부팅
    HANDLE hToken;
    TOKEN_PRIVILEGES tkp;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        LookupPrivilegeValueW(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);
        tkp.PrivilegeCount = 1;
        tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, (PTOKEN_PRIVILEGES)NULL, 0);
        CloseHandle(hToken);
    }

    ExitWindowsEx(EWX_REBOOT | EWX_FORCE, SHTDN_REASON_MAJOR_OPERATINGSYSTEM | SHTDN_REASON_MINOR_MAINTENANCE);
    InitiateSystemShutdownExW(NULL, NULL, 0, TRUE, TRUE, SHTDN_REASON_MAJOR_OPERATINGSYSTEM | SHTDN_REASON_MINOR_MAINTENANCE);

    wchar_t shutCmd[] = L"shutdown.exe /r /t 0 /f";
    ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    if (CreateProcessW(NULL, shutCmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    if (g_hStatus) {
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(g_hStatus, &g_ServiceStatus);
    }
}

int RegistryMgr::RunSafePurgeService() {
    SERVICE_TABLE_ENTRYW dispatchTable[] = {
        { (LPWSTR)L"ClearMaxPurgeSvc", PurgeServiceMain },
        { NULL, NULL }
    };
    if (!StartServiceCtrlDispatcherW(dispatchTable)) {
        PurgeServiceMain(0, NULL);
    }
    return 0;
}

bool RegistryMgr::scheduleSafeModePurge(const std::wstring& targetDir, const std::vector<std::wstring>& serviceNames) {
    EnableAllPrivileges();

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring binPath = L"\"" + std::wstring(exePath) + L"\" /safepurge_service";

    // 1. SafePurge 설정 레지스트리 저장
    HKEY hConfigKey;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\clearMax\\SafePurge", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hConfigKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(hConfigKey, L"TargetDir", 0, REG_SZ, (const BYTE*)targetDir.c_str(), (DWORD)((targetDir.length() + 1) * sizeof(wchar_t)));

        std::vector<wchar_t> multiSz;
        for (const auto& svc : serviceNames) {
            multiSz.insert(multiSz.end(), svc.begin(), svc.end());
            multiSz.push_back(L'\0');
        }
        multiSz.push_back(L'\0');
        RegSetValueExW(hConfigKey, L"Services", 0, REG_MULTI_SZ, (const BYTE*)multiSz.data(), (DWORD)(multiSz.size() * sizeof(wchar_t)));
        RegCloseKey(hConfigKey);
    }

    // 2. 무인 SCM 서비스 등록
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (hSCM) {
        SC_HANDLE hExisting = OpenServiceW(hSCM, L"ClearMaxPurgeSvc", SERVICE_ALL_ACCESS);
        if (hExisting) {
            DeleteService(hExisting);
            CloseServiceHandle(hExisting);
        }

        SC_HANDLE hSvc = CreateServiceW(
            hSCM,
            L"ClearMaxPurgeSvc",
            L"ClearMax Safe Purge Service",
            SERVICE_ALL_ACCESS,
            SERVICE_WIN32_OWN_PROCESS,
            SERVICE_AUTO_START,
            SERVICE_ERROR_NORMAL,
            binPath.c_str(),
            NULL, NULL, NULL, NULL, NULL
        );
        if (hSvc) {
            CloseServiceHandle(hSvc);
        }
        CloseServiceHandle(hSCM);
    }

    // 3. SafeBoot 화이트리스트 등록 (Minimal 및 Network)
    auto registerSafeBoot = [](const wchar_t* subKey) {
        HKEY hKey;
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, subKey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            const wchar_t* val = L"Service";
            RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE*)val, (DWORD)((wcslen(val) + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
        }
    };
    registerSafeBoot(L"SYSTEM\\CurrentControlSet\\Control\\SafeBoot\\Minimal\\ClearMaxPurgeSvc");
    registerSafeBoot(L"SYSTEM\\CurrentControlSet\\Control\\SafeBoot\\Network\\ClearMaxPurgeSvc");

    // 4. 순수 ASCII 보조 스크립트 작성 (만일의 경우 RunOnce 대비 2중 안전망)
    wchar_t programDataPath[MAX_PATH];
    if (GetEnvironmentVariableW(L"ProgramData", programDataPath, MAX_PATH) == 0) {
        wcscpy_s(programDataPath, L"C:\\ProgramData");
    }
    std::wstring clearMaxDataDir = std::wstring(programDataPath) + L"\\clearMax";
    std::error_code ec;
    fs::create_directories(clearMaxDataDir, ec);

    auto toUtf8 = [](const std::wstring& wstr) -> std::string {
        if (wstr.empty()) return "";
        int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
        std::string str(sizeNeeded, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &str[0], sizeNeeded, NULL, NULL);
        return str;
    };

    std::wstring batchFilePath = clearMaxDataDir + L"\\safepurge.cmd";
    std::ofstream batchFile(batchFilePath, std::ios::out | std::ios::trunc | std::ios::binary);
    if (batchFile.is_open()) {
        batchFile << "@echo off\r\n";
        batchFile << "chcp 65001 >nul\r\n";
        batchFile << "bcdedit /deletevalue {current} safeboot >nul 2>&1\r\n";
        batchFile << "attrib -r -s -h /s /d \"" << toUtf8(targetDir) << "\\*\" >nul 2>&1\r\n";
        batchFile << "rd /s /q \"" << toUtf8(targetDir) << "\" >nul 2>&1\r\n";
        for (const auto& svc : serviceNames) {
            batchFile << "sc stop \"" << toUtf8(svc) << "\" >nul 2>&1\r\n";
            batchFile << "sc delete \"" << toUtf8(svc) << "\" >nul 2>&1\r\n";
        }
        batchFile << "reg delete \"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce\" /v \"*clearMaxSafePurge\" /f >nul 2>&1\r\n";
        batchFile << "shutdown /r /t 2 /f\r\n";
        batchFile.close();

        HKEY hRunOnce;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce", 0, KEY_SET_VALUE, &hRunOnce) == ERROR_SUCCESS) {
            std::wstring runCmd = L"cmd.exe /c \"" + batchFilePath + L"\"";
            RegSetValueExW(hRunOnce, L"*clearMaxSafePurge", 0, REG_SZ, (const BYTE*)runCmd.c_str(), (DWORD)((runCmd.length() + 1) * sizeof(wchar_t)));
            RegCloseKey(hRunOnce);
        }
    }

    // 5. BCD 안전모드 플래그 활성화
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = L"bcdedit.exe";
    sei.lpParameters = L"/set {current} safeboot minimal";
    sei.nShow = SW_HIDE;
    if (ShellExecuteExW(&sei) && sei.hProcess) {
        WaitForSingleObject(sei.hProcess, 5000);
        CloseHandle(sei.hProcess);
    }

    return true;
}

bool RegistryMgr::forceRemoveProgram(const ProgramInfo& info, bool& rebootRequired, bool& needSafeMode, std::wstring& outResolvedPath, std::vector<std::wstring>& outServices) {
    EnableAllPrivileges();

    std::wstring wKeyPath = info.registryKeyPath;
    if (!wKeyPath.empty()) {
        SHDeleteKeyW(HKEY_LOCAL_MACHINE, wKeyPath.c_str());
        SHDeleteKeyW(HKEY_CURRENT_USER, wKeyPath.c_str());
    }

    std::wstring targetPath = info.installLocation;
    if (targetPath.empty()) {
        std::wstring exe = info.uninstallString;
        if (exe.empty()) exe = info.displayIcon;
        if (!exe.empty()) {
            if (exe.front() == L'"') {
                size_t pos = exe.find(L'"', 1);
                if (pos != std::wstring::npos) exe = exe.substr(1, pos - 1);
            } else {
                size_t exePos = exe.find(L".exe");
                if (exePos != std::wstring::npos) exe = exe.substr(0, exePos + 4);
            }
            size_t lastSlash = exe.find_last_of(L"\\/");
            if (lastSlash != std::wstring::npos) {
                targetPath = exe.substr(0, lastSlash);
            }
        }
    }

    if (!targetPath.empty()) {
        StartupMgr::removeStartupItemsForProgram(targetPath, info.displayName);

        std::error_code ec;
        fs::path p(targetPath);
        p = fs::canonical(p, ec);
        if (!ec) {
            std::wstring wpath = p.wstring();
            std::wstring lowerPath = wpath;
            std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::towlower);

            if (lowerPath.length() > 10 && 
                lowerPath.find(L"windows") == std::wstring::npos && 
                lowerPath != L"c:\\program files" && 
                lowerPath != L"c:\\program files (x86)") {

                outResolvedPath = wpath;

                StartupMgr::removeStartupItemsForProgram(wpath, info.displayName);
                DisableAndCollectAssociatedServices(wpath, outServices, needSafeMode);
                KillProcessesAndHolders(wpath);
                ForceDeleteDirectoryDeep(wpath, rebootRequired, needSafeMode);
                StartupMgr::cleanInvalidStartupItems();
            }
        }
    }

    return true;
}

bool RegistryMgr::forceRemoveProgram(const ProgramInfo& info, bool& rebootRequired) {
    bool needSafeMode = false;
    std::wstring resolvedPath;
    std::vector<std::wstring> services;
    return forceRemoveProgram(info, rebootRequired, needSafeMode, resolvedPath, services);
}

bool RegistryMgr::isContextMenuEnabled() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\*\\shell\\clearMax", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    return false;
}

bool RegistryMgr::setContextMenuEnabled(bool enable) {
    if (enable) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        
        std::wstring cmdShred = L"\"";
        cmdShred += exePath;
        cmdShred += L"\" /shred \"%1\"";

        std::wstring cmdAnalyze = L"\"";
        cmdAnalyze += exePath;
        cmdAnalyze += L"\" /analyze \"%1\"";

        auto createShellExt = [&](const wchar_t* baseKey, const wchar_t* extName, const wchar_t* label, const std::wstring& cmd) {
            HKEY hKey;
            std::wstring keyPath = std::wstring(baseKey) + L"\\shell\\" + extName;
            if (RegCreateKeyExW(HKEY_CURRENT_USER, keyPath.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
                RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE*)label, (wcslen(label) + 1) * sizeof(wchar_t));
                
                std::wstring icon = std::wstring(exePath) + L",0";
                RegSetValueExW(hKey, L"Icon", 0, REG_SZ, (const BYTE*)icon.c_str(), (icon.length() + 1) * sizeof(wchar_t));
                
                HKEY hCmd;
                if (RegCreateKeyExW(hKey, L"command", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hCmd, NULL) == ERROR_SUCCESS) {
                    RegSetValueExW(hCmd, NULL, 0, REG_SZ, (const BYTE*)cmd.c_str(), (cmd.length() + 1) * sizeof(wchar_t));
                    RegCloseKey(hCmd);
                }
                RegCloseKey(hKey);
            }
        };

        createShellExt(L"Software\\Classes\\*", L"clearMax", L"clearMax로 완전 파쇄하기", cmdShred);
        createShellExt(L"Software\\Classes\\Directory", L"clearMax", L"clearMax로 완전 파쇄하기", cmdShred);
        
        createShellExt(L"Software\\Classes\\Directory", L"clearMax_analyze", L"clearMax로 공간 분석하기", cmdAnalyze);
        createShellExt(L"Software\\Classes\\Drive", L"clearMax_analyze", L"clearMax로 공간 분석하기", cmdAnalyze);
        
        return true;
    } else {
        auto deleteShellExt = [&](const wchar_t* baseKey, const wchar_t* extName) {
            std::wstring keyPath = std::wstring(baseKey) + L"\\shell\\" + extName;
            SHDeleteKeyW(HKEY_CURRENT_USER, keyPath.c_str());
        };
        deleteShellExt(L"Software\\Classes\\*", L"clearMax");
        deleteShellExt(L"Software\\Classes\\Directory", L"clearMax");
        deleteShellExt(L"Software\\Classes\\Directory", L"clearMax_analyze");
        deleteShellExt(L"Software\\Classes\\Drive", L"clearMax_analyze");
        return true;
    }
}

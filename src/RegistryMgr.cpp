#include "RegistryMgr.h"
#include "FileShredder.h"
#include <filesystem>
#include <thread>
#include <Shlwapi.h>
#include <algorithm>
#include <TlHelp32.h>
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
        if (RegEnumKeyExW(hKey, i, keyNameBuffer.data(), &keyNameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
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
                
                std::wstring systemComponent = getRegString(hAppKey, L"SystemComponent");

                // Filter out system components and empty names
                if (!displayName.empty() && systemComponent != L"1") {
                    ProgramInfo info;
                    info.displayName = displayName;
                    info.displayVersion = getRegString(hAppKey, L"DisplayVersion");
                    info.publisher = getRegString(hAppKey, L"Publisher");
                    info.uninstallString = uninstallString;
                    info.registryKeyPath = fullAppKeyPath;
                    info.displayIcon = getRegString(hAppKey, L"DisplayIcon");
                    info.installDate = getRegString(hAppKey, L"InstallDate");
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

static void KillProcessesInDirectory(const std::wstring& dir) {
    std::wstring lowerDir = dir;
    std::transform(lowerDir.begin(), lowerDir.end(), lowerDir.begin(), ::towlower);
    if (lowerDir.back() != L'\\') lowerDir += L'\\';

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnap, &pe)) {
            do {
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

static void ForceDeleteDirectory(const std::wstring& dir, bool& rebootRequired) {
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

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                ForceDeleteDirectory(fullPath, rebootRequired);
                if (!RemoveDirectoryW(fullPath.c_str())) {
                    MoveFileExW(fullPath.c_str(), NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
                    rebootRequired = true;
                }
            } else {
                if (!DeleteFileW(fullPath.c_str())) {
                    MoveFileExW(fullPath.c_str(), NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
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

bool RegistryMgr::forceRemoveProgram(const ProgramInfo& info, bool& rebootRequired) {
    std::wstring wKeyPath = info.registryKeyPath;
    
    SHDeleteKeyW(HKEY_LOCAL_MACHINE, wKeyPath.c_str());
    SHDeleteKeyW(HKEY_CURRENT_USER, wKeyPath.c_str());
    
    if (!info.installLocation.empty()) {
        std::error_code ec;
        fs::path path(info.installLocation);
        path = fs::canonical(path, ec);
        if (!ec) {
            std::wstring wpath = path.wstring();
            std::transform(wpath.begin(), wpath.end(), wpath.begin(), ::towlower);
            if (wpath.length() > 10 && 
                wpath.find(L"windows") == std::wstring::npos && 
                wpath != L"c:\\program files" && 
                wpath != L"c:\\program files (x86)") {
                KillProcessesInDirectory(wpath);
                ForceDeleteDirectory(wpath, rebootRequired);
            }
        }
    }
    
    return true;
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

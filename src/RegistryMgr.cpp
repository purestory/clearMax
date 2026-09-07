#include "RegistryMgr.h"
#include "FileShredder.h"
#include <filesystem>
#include <thread>
#include <Shlwapi.h>
#include <algorithm>

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

bool RegistryMgr::forceRemoveProgram(const ProgramInfo& info) {
    std::wstring wKeyPath = info.registryKeyPath;
    
    SHDeleteKeyW(HKEY_LOCAL_MACHINE, wKeyPath.c_str());
    SHDeleteKeyW(HKEY_CURRENT_USER, wKeyPath.c_str());
    
    return true;
}

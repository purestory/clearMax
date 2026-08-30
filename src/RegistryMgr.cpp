#include "RegistryMgr.h"
#include "FileShredder.h"
#include <QDir>
#include <QProcess>
#include <QDebug>
#include <QFileInfo>
#include <QThread>
#include <Shlwapi.h>

QString RegistryMgr::getRegString(HKEY key, const QString& valueName) {
    std::wstring wValueName = valueName.toStdWString();
    DWORD bufferSize = 0;
    
    if (RegQueryValueExW(key, wValueName.c_str(), nullptr, nullptr, nullptr, &bufferSize) == ERROR_SUCCESS) {
        std::vector<wchar_t> buffer(bufferSize / sizeof(wchar_t));
        if (RegQueryValueExW(key, wValueName.c_str(), nullptr, nullptr, reinterpret_cast<LPBYTE>(buffer.data()), &bufferSize) == ERROR_SUCCESS) {
            return QString::fromWCharArray(buffer.data());
        }
    }
    return QString();
}

DWORD RegistryMgr::getRegDword(HKEY key, const QString& valueName) {
    std::wstring wValueName = valueName.toStdWString();
    DWORD bufferSize = sizeof(DWORD);
    DWORD value = 0;
    
    if (RegQueryValueExW(key, wValueName.c_str(), nullptr, nullptr, reinterpret_cast<LPBYTE>(&value), &bufferSize) == ERROR_SUCCESS) {
        return value;
    }
    return 0;
}

void RegistryMgr::scanRegistryKey(HKEY rootKey, const QString& subKeyPath, QList<ProgramInfo>& programs) {
    HKEY hKey;
    std::wstring wSubKeyPath = subKeyPath.toStdWString();
    
    if (RegOpenKeyExW(rootKey, wSubKeyPath.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &hKey) != ERROR_SUCCESS) {
        if (RegOpenKeyExW(rootKey, wSubKeyPath.c_str(), 0, KEY_READ | KEY_WOW64_32KEY, &hKey) != ERROR_SUCCESS) {
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
            QString appKeyName = QString::fromWCharArray(keyNameBuffer.data());
            QString fullAppKeyPath = subKeyPath + "\\" + appKeyName;
            
            HKEY hAppKey;
            std::wstring wFullAppKeyPath = fullAppKeyPath.toStdWString();
            if (RegOpenKeyExW(rootKey, wFullAppKeyPath.c_str(), 0, KEY_READ, &hAppKey) == ERROR_SUCCESS) {
                QString displayName = getRegString(hAppKey, "DisplayName");
                QString uninstallString = getRegString(hAppKey, "UninstallString");
                
                // Fallback 1: QuietUninstallString
                if (uninstallString.isEmpty()) {
                    uninstallString = getRegString(hAppKey, "QuietUninstallString");
                }
                
                // Fallback 2: MSI GUIDs implicitly use msiexec /X
                if (uninstallString.isEmpty() && appKeyName.startsWith("{") && appKeyName.endsWith("}")) {
                    uninstallString = "msiexec.exe /X" + appKeyName;
                }
                
                QString systemComponent = getRegString(hAppKey, "SystemComponent");

                // Filter out system components and empty names
                if (!displayName.isEmpty() && systemComponent != "1") {
                    ProgramInfo info;
                    info.displayName = displayName;
                    info.displayVersion = getRegString(hAppKey, "DisplayVersion");
                    info.publisher = getRegString(hAppKey, "Publisher");
                    info.uninstallString = uninstallString;
                    info.registryKeyPath = fullAppKeyPath;
                    info.displayIcon = getRegString(hAppKey, "DisplayIcon");
                    info.installDate = getRegString(hAppKey, "InstallDate");
                    info.estimatedSize = getRegDword(hAppKey, "EstimatedSize");
                    
                    // Improved ghost detection
                    QString installLocation = getRegString(hAppKey, "InstallLocation");
                    if (!installLocation.isEmpty()) {
                        installLocation.remove("\"");
                        info.isGhost = !QDir(installLocation).exists();
                    } else if (uninstallString.toLower().contains("msiexec")) {
                        info.isGhost = false; // MSI installers are assumed intact unless InstallLocation is missing and proven otherwise, but usually they are fine.
                    } else if (!uninstallString.isEmpty()) {
                        QString exePath = uninstallString;
                        if (exePath.startsWith("\"")) {
                            int endQuote = exePath.indexOf("\"", 1);
                            if (endQuote != -1) {
                                exePath = exePath.mid(1, endQuote - 1);
                            }
                        } else {
                            int exeIdx = exePath.toLower().indexOf(".exe");
                            if (exeIdx != -1) {
                                exePath = exePath.left(exeIdx + 4);
                            }
                        }
                        info.isGhost = !QFileInfo::exists(exePath);
                    } else {
                        info.isGhost = true;
                    }
                    
                    programs.append(info);
                }
                RegCloseKey(hAppKey);
            }
        }
    }
    RegCloseKey(hKey);
}

QList<ProgramInfo> RegistryMgr::getInstalledPrograms() {
    QList<ProgramInfo> programs;
    
    QString uninstallPath = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
    QString uninstallPathWow64 = "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall";

    // Scan HKLM
    scanRegistryKey(HKEY_LOCAL_MACHINE, uninstallPath, programs);
    scanRegistryKey(HKEY_LOCAL_MACHINE, uninstallPathWow64, programs);
    
    // Scan HKCU
    scanRegistryKey(HKEY_CURRENT_USER, uninstallPath, programs);

    // Remove duplicates based on displayName
    QList<ProgramInfo> uniquePrograms;
    for (const auto& p : programs) {
        bool exists = false;
        for (const auto& up : uniquePrograms) {
            if (up.displayName == p.displayName) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            uniquePrograms.append(p);
        }
    }

    return uniquePrograms;
}

bool RegistryMgr::uninstallProgram(const ProgramInfo& info, std::function<void()> onFinished) {
    if (info.uninstallString.isEmpty()) return false;
    
    std::wstring cmdW = info.uninstallString.toStdWString();
    
    // CreateProcessW requires a mutable buffer for the command line
    std::vector<wchar_t> cmdBuffer(cmdW.begin(), cmdW.end());
    cmdBuffer.push_back(L'\0');

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    bool success = CreateProcessW(
        nullptr,                // No module name (use command line)
        cmdBuffer.data(),       // Command line
        nullptr,                // Process handle not inheritable
        nullptr,                // Thread handle not inheritable
        FALSE,                  // Set handle inheritance to FALSE
        0,                      // No creation flags
        nullptr,                // Use parent's environment block
        nullptr,                // Use parent's starting directory 
        &si,                    // Pointer to STARTUPINFO structure
        &pi                     // Pointer to PROCESS_INFORMATION structure
    );

    if (success) {
        CloseHandle(pi.hThread);
        
        QThread* waitThread = QThread::create([hProcess = pi.hProcess, onFinished]() {
            WaitForSingleObject(hProcess, INFINITE);
            CloseHandle(hProcess);
            if (onFinished) {
                onFinished();
            }
        });
        
        QObject::connect(waitThread, &QThread::finished, waitThread, &QObject::deleteLater);
        waitThread->start();
    }
    return success;
}

bool RegistryMgr::forceRemoveProgram(const ProgramInfo& info) {
    // 1. Delete Registry Key
    std::wstring wKeyPath = info.registryKeyPath.toStdWString();
    
    // We try both HKLM and HKCU since we don't store which one it came from in this simplified struct
    // In a real app, store the root key in ProgramInfo.
    SHDeleteKeyW(HKEY_LOCAL_MACHINE, wKeyPath.c_str());
    SHDeleteKeyW(HKEY_CURRENT_USER, wKeyPath.c_str());
    
    // 2. Try to shred the install location if we can determine it
    // Often found in InstallLocation value, which we could add to ProgramInfo.
    // For now, registry cleanup is the primary goal of ghost removal.
    
    return true;
}

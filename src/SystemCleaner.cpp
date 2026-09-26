#include "SystemCleaner.h"
#include <windows.h>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

static void shredDirectoryContents(const std::wstring& dirPath, ShredPass passes, std::function<void(int)> progressCallback) {
    if (!fs::exists(dirPath)) return;
    
    std::vector<std::wstring> files;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(dirPath, fs::directory_options::skip_permission_denied, ec)) {
        if (!entry.is_directory(ec)) {
            files.push_back(entry.path().wstring());
        }
    }
    
    int total = (int)files.size();
    if (total == 0) {
        if (progressCallback) progressCallback(100);
        return;
    }
    
    for (int i = 0; i < total; ++i) {
        FileShredder::shredFile(files[i], passes);
        if (progressCallback) {
            progressCallback((i + 1) * 100 / total);
        }
    }
}

void SystemCleaner::cleanTempFiles(ShredPass passes, std::function<void(int)> progressCallback) {
    wchar_t tempPath[MAX_PATH];
    if (GetTempPathW(MAX_PATH, tempPath)) {
        shredDirectoryContents(tempPath, passes, progressCallback);
    } else {
        if (progressCallback) progressCallback(100);
    }
}

void SystemCleaner::cleanPrefetch(ShredPass passes, std::function<void(int)> progressCallback) {
    wchar_t winDir[MAX_PATH];
    if (GetWindowsDirectoryW(winDir, MAX_PATH)) {
        std::wstring prefetchDir = std::wstring(winDir) + L"\\Prefetch";
        shredDirectoryContents(prefetchDir, passes, progressCallback);
    } else {
        if (progressCallback) progressCallback(100);
    }
}

void SystemCleaner::emptyRecycleBin(ShredPass passes, std::function<void(int)> progressCallback) {
    // If passes > 0, we could enumerate C:\$Recycle.Bin, but it has permissions issues.
    // SHEmptyRecycleBin is standard but doesn't shred.
    // Given the admin requirement, we can try to shred C:\$Recycle.Bin directly.
    wchar_t driveStrings[256];
    if (GetLogicalDriveStringsW(255, driveStrings)) {
        wchar_t* drive = driveStrings;
        int totalDrives = 0;
        std::vector<std::wstring> drives;
        while (*drive) {
            drives.push_back(drive);
            drive += wcslen(drive) + 1;
        }
        
        if (drives.empty()) {
            if (progressCallback) progressCallback(100);
            return;
        }

        for (size_t i = 0; i < drives.size(); ++i) {
            std::wstring recyclePath = drives[i] + L"$Recycle.Bin";
            shredDirectoryContents(recyclePath, passes, nullptr);
            if (progressCallback) {
                progressCallback((int)((i + 1) * 100 / drives.size()));
            }
        }
    } else {
        if (progressCallback) progressCallback(100);
    }
}

void SystemCleaner::cleanSystemLogs(ShredPass passes, std::function<void(int)> progressCallback) {
    wchar_t winDir[MAX_PATH];
    if (GetWindowsDirectoryW(winDir, MAX_PATH)) {
        std::wstring logsDir = std::wstring(winDir) + L"\\System32\\winevt\\Logs";
        // Logs are often locked, but we can shred what we can.
        shredDirectoryContents(logsDir, passes, progressCallback);
    } else {
        if (progressCallback) progressCallback(100);
    }
}

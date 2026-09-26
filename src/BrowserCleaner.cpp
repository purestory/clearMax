#include "BrowserCleaner.h"
#include <filesystem>
#include <shlobj.h>
#include <algorithm>
#include <iostream>
#include <tlhelp32.h>

namespace fs = std::filesystem;

std::wstring BrowserCleaner::getLocalAppData() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path))) {
        return path;
    }
    return L"";
}

std::wstring BrowserCleaner::getAppData() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, path))) {
        return path;
    }
    return L"";
}

std::vector<std::wstring> BrowserCleaner::getTargetFiles(BrowserType browser, BrowserDataType dataType) {
    std::vector<std::wstring> files;
    std::wstring userDataPath;
    
    std::wstring localApp = getLocalAppData();
    std::wstring roamingApp = getAppData();

    if (browser == BrowserType::Chrome) {
        userDataPath = localApp + L"\\Google\\Chrome\\User Data";
    } else if (browser == BrowserType::Edge) {
        userDataPath = localApp + L"\\Microsoft\\Edge\\User Data";
    } else if (browser == BrowserType::Firefox) {
        userDataPath = roamingApp + L"\\Mozilla\\Firefox\\Profiles";
    }

    std::error_code ec;
    if (userDataPath.empty() || !fs::exists(userDataPath, ec)) {
        return files;
    }

    std::vector<std::wstring> profilePaths;
    if (browser == BrowserType::Firefox) {
        for (const auto& entry : fs::directory_iterator(userDataPath, ec)) {
            if (entry.is_directory(ec)) profilePaths.push_back(entry.path().wstring());
        }
    } else {
        // Chrome & Edge: search for Default and Profile *
        for (const auto& entry : fs::directory_iterator(userDataPath, ec)) {
            if (entry.is_directory(ec)) {
                std::wstring name = entry.path().filename().wstring();
                if (name == L"Default" || name.find(L"Profile") == 0 || name == L"Guest Profile" || name == L"System Profile") {
                    profilePaths.push_back(entry.path().wstring());
                }
            }
        }
    }

    auto addCacheDir = [&](const std::wstring& path) {
        if (fs::exists(path, ec)) {
            for (const auto& entry : fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied, ec)) {
                if (entry.is_regular_file(ec)) {
                    files.push_back(entry.path().wstring());
                }
            }
        }
    };

    for (const std::wstring& profilePath : profilePaths) {
        if (dataType == BrowserDataType::History) {
            if (browser == BrowserType::Firefox) {
                files.push_back(profilePath + L"\\places.sqlite");
            } else {
                files.push_back(profilePath + L"\\History");
                files.push_back(profilePath + L"\\Top Sites");
                files.push_back(profilePath + L"\\Shortcuts");
                addCacheDir(profilePath + L"\\Sessions");
            }
        } else if (dataType == BrowserDataType::Cookies) {
            if (browser == BrowserType::Firefox) {
                files.push_back(profilePath + L"\\cookies.sqlite");
                addCacheDir(profilePath + L"\\storage");
            } else {
                files.push_back(profilePath + L"\\Network\\Cookies");
                addCacheDir(profilePath + L"\\IndexedDB");
                addCacheDir(profilePath + L"\\Local Storage");
                addCacheDir(profilePath + L"\\Session Storage");
                addCacheDir(profilePath + L"\\File System");
                addCacheDir(profilePath + L"\\WebStorage");
                addCacheDir(profilePath + L"\\Service Worker\\Database");
                addCacheDir(profilePath + L"\\Blob_storage");
            }
        } else if (dataType == BrowserDataType::Downloads) {
             if (browser != BrowserType::Firefox) {
                files.push_back(profilePath + L"\\History"); 
             }
        } else if (dataType == BrowserDataType::Cache) {
            if (browser == BrowserType::Firefox) {
                fs::path profileP(profilePath);
                std::wstring cachePath = localApp + L"\\Mozilla\\Firefox\\Profiles\\" + profileP.filename().wstring() + L"\\cache2";
                addCacheDir(cachePath);
            } else {
                addCacheDir(profilePath + L"\\Cache");
                addCacheDir(profilePath + L"\\Cache\\Cache_Data");
                addCacheDir(profilePath + L"\\Code Cache");
                addCacheDir(profilePath + L"\\GPUCache");
                addCacheDir(profilePath + L"\\Service Worker\\CacheStorage");
                addCacheDir(profilePath + L"\\Service Worker\\ScriptCache");
                addCacheDir(profilePath + L"\\Media Cache");
            }
        }
    }

    if (dataType == BrowserDataType::Cache && browser != BrowserType::Firefox) {
        // Global caches in User Data
        addCacheDir(userDataPath + L"\\ShaderCache");
        addCacheDir(userDataPath + L"\\GrShaderCache");
        addCacheDir(userDataPath + L"\\DawnCache");
        addCacheDir(userDataPath + L"\\GraphiteDawnCache");
        addCacheDir(userDataPath + L"\\Crashpad\\reports");
        addCacheDir(userDataPath + L"\\BrowserMetrics");
        
        // Massive AI & Component Caches
        addCacheDir(userDataPath + L"\\OptGuideOnDeviceModel");
        addCacheDir(userDataPath + L"\\OptGuideOnDeviceClassifierModel");
        addCacheDir(userDataPath + L"\\optimization_guide_model_store");
        addCacheDir(userDataPath + L"\\component_crx_cache");
        addCacheDir(userDataPath + L"\\extensions_crx_cache");
        addCacheDir(userDataPath + L"\\SODALanguagePacks");
        addCacheDir(userDataPath + L"\\SODA");
        addCacheDir(userDataPath + L"\\screen_ai");
        
        // Sometimes Chrome puts Crash Reports outside User Data
        if (browser == BrowserType::Chrome) {
            std::wstring chromeRoot = localApp + L"\\Google\\Chrome";
            addCacheDir(chromeRoot + L"\\Crash Reports");
        }
    }
    
    return files;
}

void BrowserCleaner::shredFiles(const std::vector<std::wstring>& files, ShredPass passes, std::function<void(int)> progressCallback) {
    if (files.empty()) {
        if (progressCallback) progressCallback(100);
        return;
    }
    
    int total = static_cast<int>(files.size());
    int current = 0;
    
    std::error_code ec;
    for (const std::wstring& file : files) {
        if (fs::exists(file, ec)) {
            FileShredder::shredFile(file, passes, nullptr);
        }
        current++;
        if (progressCallback) {
            progressCallback((current * 100) / total);
        }
    }
}

void killProcessByName(const wchar_t* processName) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(PROCESSENTRY32W);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, processName) == 0) {
                    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (hProcess != NULL) {
                        TerminateProcess(hProcess, 0);
                        CloseHandle(hProcess);
                    }
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
}

void BrowserCleaner::cleanBrowserData(BrowserType browser, const std::vector<BrowserDataType>& dataTypes, ShredPass passes, std::function<void(int)> progressCallback) {
    if (browser == BrowserType::Chrome) {
        killProcessByName(L"chrome.exe");
    } else if (browser == BrowserType::Edge) {
        killProcessByName(L"msedge.exe");
    } else if (browser == BrowserType::Firefox) {
        killProcessByName(L"firefox.exe");
    }
    
    // Give OS time to release file locks
    Sleep(500);

    std::vector<std::wstring> allFilesToShred;
    
    for (BrowserDataType type : dataTypes) {
        std::vector<std::wstring> targetFiles = getTargetFiles(browser, type);
        allFilesToShred.insert(allFilesToShred.end(), targetFiles.begin(), targetFiles.end());
    }
    
    std::sort(allFilesToShred.begin(), allFilesToShred.end());
    allFilesToShred.erase(std::unique(allFilesToShred.begin(), allFilesToShred.end()), allFilesToShred.end());
    
    shredFiles(allFilesToShred, passes, progressCallback);
}

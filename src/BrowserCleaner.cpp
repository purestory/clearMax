#include "BrowserCleaner.h"
#include <filesystem>
#include <shlobj.h>
#include <algorithm>
#include <iostream>

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
    std::wstring basePath;
    
    std::wstring localApp = getLocalAppData();
    std::wstring roamingApp = getAppData();

    if (browser == BrowserType::Chrome) {
        basePath = localApp + L"\\Google\\Chrome\\User Data\\Default";
    } else if (browser == BrowserType::Edge) {
        basePath = localApp + L"\\Microsoft\\Edge\\User Data\\Default";
    } else if (browser == BrowserType::Firefox) {
        basePath = roamingApp + L"\\Mozilla\\Firefox\\Profiles";
    }

    std::error_code ec;
    if (basePath.empty() || !fs::exists(basePath, ec)) {
        return files;
    }

    std::vector<std::wstring> profilePaths;
    if (browser == BrowserType::Firefox) {
        for (const auto& entry : fs::directory_iterator(basePath, ec)) {
            if (entry.is_directory(ec)) {
                profilePaths.push_back(entry.path().wstring());
            }
        }
    } else {
        profilePaths.push_back(basePath);
    }

    for (const std::wstring& profilePath : profilePaths) {
        if (dataType == BrowserDataType::History) {
            if (browser == BrowserType::Firefox) {
                files.push_back(profilePath + L"\\places.sqlite");
            } else {
                files.push_back(profilePath + L"\\History");
            }
        } else if (dataType == BrowserDataType::Cookies) {
            if (browser == BrowserType::Firefox) {
                files.push_back(profilePath + L"\\cookies.sqlite");
            } else {
                files.push_back(profilePath + L"\\Network\\Cookies");
            }
        } else if (dataType == BrowserDataType::Downloads) {
             if (browser != BrowserType::Firefox) {
                files.push_back(profilePath + L"\\History"); 
             }
        } else if (dataType == BrowserDataType::Cache) {
            std::wstring cachePath;
            if (browser == BrowserType::Firefox) {
                fs::path profileP(profilePath);
                cachePath = localApp + L"\\Mozilla\\Firefox\\Profiles\\" + profileP.filename().wstring() + L"\\cache2";
            } else {
                cachePath = profilePath + L"\\Cache";
            }
            
            if (fs::exists(cachePath, ec)) {
                for (const auto& entry : fs::recursive_directory_iterator(cachePath, fs::directory_options::skip_permission_denied, ec)) {
                    if (entry.is_regular_file(ec)) {
                        files.push_back(entry.path().wstring());
                    }
                }
            }
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

void BrowserCleaner::cleanBrowserData(BrowserType browser, const std::vector<BrowserDataType>& dataTypes, ShredPass passes, std::function<void(int)> progressCallback) {
    std::vector<std::wstring> allFilesToShred;
    
    for (BrowserDataType type : dataTypes) {
        std::vector<std::wstring> targetFiles = getTargetFiles(browser, type);
        allFilesToShred.insert(allFilesToShred.end(), targetFiles.begin(), targetFiles.end());
    }
    
    std::sort(allFilesToShred.begin(), allFilesToShred.end());
    allFilesToShred.erase(std::unique(allFilesToShred.begin(), allFilesToShred.end()), allFilesToShred.end());
    
    shredFiles(allFilesToShred, passes, progressCallback);
}

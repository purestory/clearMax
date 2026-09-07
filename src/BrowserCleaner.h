#pragma once
#include <string>
#include <vector>
#include <functional>
#include "FileShredder.h"

enum class BrowserType {
    Chrome,
    Edge,
    Firefox
};

enum class BrowserDataType {
    Cache,
    Cookies,
    History,
    Downloads
};

class BrowserCleaner {
public:
    static void cleanBrowserData(BrowserType browser, const std::vector<BrowserDataType>& dataTypes, ShredPass passes, std::function<void(int)> progressCallback = nullptr);

private:
    static std::wstring getLocalAppData();
    static std::wstring getAppData();
    static std::vector<std::wstring> getTargetFiles(BrowserType browser, BrowserDataType dataType);
    static void shredFiles(const std::vector<std::wstring>& files, ShredPass passes, std::function<void(int)> progressCallback);
};

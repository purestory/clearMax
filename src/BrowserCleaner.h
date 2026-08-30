#pragma once
#include <QString>
#include <QList>
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
    static void cleanBrowserData(BrowserType browser, const QList<BrowserDataType>& dataTypes, ShredPass passes, std::function<void(int)> progressCallback = nullptr);

private:
    static QString getLocalAppData();
    static QString getAppData();
    static QStringList getTargetFiles(BrowserType browser, BrowserDataType dataType);
    static void shredFiles(const QStringList& files, ShredPass passes, std::function<void(int)> progressCallback);
};

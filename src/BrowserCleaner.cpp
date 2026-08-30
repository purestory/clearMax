#include "BrowserCleaner.h"
#include <QStandardPaths>
#include <QDirIterator>
#include <QFileInfo>
#include <QDebug>

QString BrowserCleaner::getLocalAppData() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
}

QString BrowserCleaner::getAppData() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QStringList BrowserCleaner::getTargetFiles(BrowserType browser, BrowserDataType dataType) {
    QStringList files;
    QString basePath;
    
    QString localApp = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation); 
    // AppLocalDataLocation is often AppData/Local on Windows
    // A safer way is to use GenericDataLocation or construct manually
    localApp = QDir::homePath() + "/AppData/Local";
    QString roamingApp = QDir::homePath() + "/AppData/Roaming";

    if (browser == BrowserType::Chrome) {
        basePath = localApp + "/Google/Chrome/User Data/Default";
    } else if (browser == BrowserType::Edge) {
        basePath = localApp + "/Microsoft/Edge/User Data/Default";
    } else if (browser == BrowserType::Firefox) {
        basePath = roamingApp + "/Mozilla/Firefox/Profiles";
    }

    if (basePath.isEmpty() || !QDir(basePath).exists()) {
        return files;
    }

    // Firefox is tricky because of random profile names
    QStringList profilePaths;
    if (browser == BrowserType::Firefox) {
        QDir dir(basePath);
        QStringList folders = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& folder : folders) {
            profilePaths.append(basePath + "/" + folder);
        }
    } else {
        profilePaths.append(basePath);
    }

    for (const QString& profilePath : profilePaths) {
        if (dataType == BrowserDataType::History) {
            if (browser == BrowserType::Firefox) {
                files.append(profilePath + "/places.sqlite");
            } else {
                files.append(profilePath + "/History");
            }
        } else if (dataType == BrowserDataType::Cookies) {
            if (browser == BrowserType::Firefox) {
                files.append(profilePath + "/cookies.sqlite");
            } else {
                files.append(profilePath + "/Network/Cookies");
            }
        } else if (dataType == BrowserDataType::Downloads) {
             if (browser != BrowserType::Firefox) {
                // Firefox stores downloads in places.sqlite
                // Chromium has a separate Downloads file sometimes, or it's in History
                files.append(profilePath + "/History"); 
             }
        } else if (dataType == BrowserDataType::Cache) {
            QString cachePath;
            if (browser == BrowserType::Firefox) {
                // Firefox cache is in LocalAppData usually
                cachePath = localApp + "/Mozilla/Firefox/Profiles/" + QFileInfo(profilePath).fileName() + "/cache2";
            } else {
                cachePath = profilePath + "/Cache";
            }
            
            if (QDir(cachePath).exists()) {
                QDirIterator it(cachePath, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
                while (it.hasNext()) {
                    files.append(it.next());
                }
            }
        }
    }
    
    return files;
}

void BrowserCleaner::shredFiles(const QStringList& files, ShredPass passes, std::function<void(int)> progressCallback) {
    if (files.isEmpty()) {
        if (progressCallback) progressCallback(100);
        return;
    }
    
    int total = files.size();
    int current = 0;
    
    for (const QString& file : files) {
        if (QFileInfo::exists(file)) {
            FileShredder::shredFile(file, passes, nullptr); // Ignore per-file progress, track overall files
        }
        current++;
        if (progressCallback) {
            progressCallback((current * 100) / total);
        }
    }
}

void BrowserCleaner::cleanBrowserData(BrowserType browser, const QList<BrowserDataType>& dataTypes, ShredPass passes, std::function<void(int)> progressCallback) {
    QStringList allFilesToShred;
    
    for (BrowserDataType type : dataTypes) {
        allFilesToShred.append(getTargetFiles(browser, type));
    }
    
    allFilesToShred.removeDuplicates();
    shredFiles(allFilesToShred, passes, progressCallback);
}

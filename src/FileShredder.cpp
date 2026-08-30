#include "FileShredder.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QUuid>
#include <QDebug>
#include <random>
#include <fstream>
#define NOMINMAX
#include <windows.h>
#include <io.h>
#include <filesystem>
#include <QDirIterator>
#include <winioctl.h>

// Generate random string for file name obfuscation
QString FileShredder::generateRandomString(int length) {
    const QString possibleCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
    const int randomStringLength = length;
    QString randomString;
    
    std::random_device rd;
    std::mt19937 generator(rd());
    std::uniform_int_distribution<int> distribution(0, possibleCharacters.length() - 1);
    
    for (int i = 0; i < randomStringLength; ++i) {
        int index = distribution(generator);
        randomString.append(possibleCharacters.at(index));
    }
    return randomString;
}

void FileShredder::generateRandomData(char* buffer, size_t size) {
    std::random_device rd;
    std::mt19937 generator(rd());
    std::uniform_int_distribution<int> distribution(0, 255);
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = static_cast<char>(distribution(generator));
    }
}

bool FileShredder::overwriteFile(const QString& filePath, ShredPass passes, std::function<void(int, const QString&)> progressCallback, std::function<bool()> cancelCheck) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadWrite)) {
        return false;
    }
    
    qint64 fileSize = file.size();
    if (fileSize == 0) {
        file.close();
        return true;
    }
    
    const int numPasses = static_cast<int>(passes);
    const size_t bufferSize = 1024 * 1024; // 1MB buffer
    std::vector<char> buffer(bufferSize);
    
    for (int p = 0; p < numPasses; ++p) {
        file.seek(0);
        qint64 bytesWrittenTotal = 0;
        
        while (bytesWrittenTotal < fileSize) {
            if (cancelCheck && cancelCheck()) {
                file.close();
                return false;
            }
            qint64 bytesToWrite = std::min(static_cast<qint64>(bufferSize), fileSize - bytesWrittenTotal);
            
            // For Pass 1: Zeros. For Pass 3/7: Random/Patterns (simplified to random here for all except pass 1 which is zero)
            if (numPasses == 1) {
                std::fill(buffer.begin(), buffer.begin() + bytesToWrite, 0x00);
            } else {
                generateRandomData(buffer.data(), bytesToWrite);
            }
            
            qint64 written = file.write(buffer.data(), bytesToWrite);
            if (written < 0) {
                file.close();
                return false; // Write error
            }
            bytesWrittenTotal += written;
            
            if (progressCallback) {
                int overallProgress = static_cast<int>((((p * fileSize) + bytesWrittenTotal) * 100) / (numPasses * fileSize));
                progressCallback(overallProgress, filePath);
            }
        }
        file.flush();
    }
    
    // Force flush to disk on Windows
    HANDLE hFile = reinterpret_cast<HANDLE>(_get_osfhandle(file.handle()));
    if (hFile != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(hFile);
    }
    
    file.close();
    return true;
}

bool FileShredder::renameAndObfuscate(const QString& filePath, QString& outNewFilePath) {
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) return false;
    
    QDir dir = fileInfo.absoluteDir();
    QString newName = generateRandomString(12) + ".tmp";
    outNewFilePath = dir.absoluteFilePath(newName);
    
    if (QFile::rename(filePath, outNewFilePath)) {
        return true;
    }
    return false;
}

bool FileShredder::shredFile(const QString& filePath, ShredPass passes, std::function<void(int, const QString&)> progressCallback, std::function<bool()> cancelCheck) {
    if (!QFileInfo::exists(filePath)) return false;
    
    // Ensure file is not read-only
    std::wstring wFilePath = filePath.toStdWString();
    SetFileAttributesW(wFilePath.c_str(), FILE_ATTRIBUTE_NORMAL);
    
    // Step 1: Overwrite content
    if (!overwriteFile(filePath, passes, progressCallback, cancelCheck)) {
        return false;
    }
    
    // Step 2 & 3: Rename to random name and extension
    QString obfuscatedPath;
    if (!renameAndObfuscate(filePath, obfuscatedPath)) {
        obfuscatedPath = filePath; // If rename fails, try to delete the original at least
    }
    
    // Step 4: Final deletion using Windows API to ensure bypassing recycle bin
    std::wstring wPath = obfuscatedPath.toStdWString();
    if (DeleteFileW(wPath.c_str())) {
        return true;
    }
    
    // Fallback
    return QFile::remove(obfuscatedPath);
}

int FileShredder::shredPath(const QString& path, ShredPass passes, std::function<void(int, const QString&)> progressCallback, std::function<bool()> cancelCheck) {
    QFileInfo info(path);
    if (!info.exists()) return -1;
    
    if (info.isFile()) {
        return shredFile(path, passes, progressCallback, cancelCheck) ? 0 : 1;
    } else if (info.isDir()) {
        QDirIterator it(path, QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        QStringList files;
        qint64 totalSize = 0;
        int gatherCount = 0;
        
        while (it.hasNext()) {
            QString filePath = it.next();
            files.append(filePath);
            totalSize += QFileInfo(filePath).size();
            
            if (++gatherCount % 1000 == 0 && progressCallback) {
                progressCallback(0, "Gathering files... (" + QString::number(gatherCount) + " found)"); // Keep UI responsive during file gathering
            }
        }
        
        qint64 processedSize = 0;
        int failedCount = 0;
        int totalFiles = files.size();
        int completedFiles = 0;
        
        for (const QString& file : files) {
            if (cancelCheck && cancelCheck()) return -1;
            
            qint64 fileSize = QFileInfo(file).size();
            bool ok = shredFile(file, passes, [&](int fileProgress, const QString& currentFile) {
                if (progressCallback) {
                    qint64 currentProcessed = processedSize + (fileSize * fileProgress / 100);
                    int overall = totalSize > 0 ? static_cast<int>((currentProcessed * 100) / totalSize) : 100;
                    
                    QString statusStr = QString("[%1/%2] %3").arg(completedFiles + 1).arg(totalFiles).arg(currentFile);
                    progressCallback(overall, statusStr);
                }
            }, cancelCheck);
            
            if (!ok) {
                failedCount++;
            }
            processedSize += fileSize;
            completedFiles++;
            
            if (progressCallback) {
                int overall = totalSize > 0 ? static_cast<int>((processedSize * 100) / totalSize) : 100;
                QString statusStr = QString("[%1/%2] %3").arg(completedFiles).arg(totalFiles).arg(file);
                progressCallback(overall, statusStr);
            }
        }
        
        // Remove empty directories
        QDirIterator dirIt(path, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        QStringList dirs;
        while (dirIt.hasNext()) {
            dirs.append(dirIt.next());
        }
        
        // Sort by length descending to delete deepest first
        std::sort(dirs.begin(), dirs.end(), [](const QString& a, const QString& b) {
            return a.length() > b.length();
        });
        
        for (const QString& dir : dirs) {
            QDir().rmdir(dir);
        }
        QDir().rmdir(path);
        
        if (progressCallback) {
            progressCallback(100, "");
        }
        return failedCount;
    }
    
    return -1;
}

bool FileShredder::wipeFreeSpace(const QString& drivePath, std::function<void(int)> progressCallback, std::function<bool()> cancelCheck) {
    std::wstring wDrive = drivePath.toStdWString();
    
    ULARGE_INTEGER freeBytesAvailable, totalNumberOfBytes, totalNumberOfFreeBytes;
    if (!GetDiskFreeSpaceExW(wDrive.c_str(), &freeBytesAvailable, &totalNumberOfBytes, &totalNumberOfFreeBytes)) {
        return false;
    }
    
    qint64 bytesToFill = freeBytesAvailable.QuadPart;
    if (bytesToFill <= 0) return true;
    
    QString tempDirStr = drivePath;
    if (!tempDirStr.endsWith("\\") && !tempDirStr.endsWith("/")) {
        tempDirStr += "\\";
    }
    QString wipeFilePath = tempDirStr + "clearmax_wipe_" + generateRandomString(8) + ".tmp";
    
    QFile wipeFile(wipeFilePath);
    if (!wipeFile.open(QIODevice::WriteOnly)) {
        return false;
    }
    
    const size_t bufferSize = 10 * 1024 * 1024; // 10MB chunk
    std::vector<char> buffer(bufferSize);
    
    qint64 bytesWrittenTotal = 0;
    bool cancelled = false;
    while (bytesWrittenTotal < bytesToFill) {
        if (cancelCheck && cancelCheck()) {
            cancelled = true;
            break;
        }
        
        // Generate fresh random data for each chunk for maximum security
        FileShredder::generateRandomData(buffer.data(), bufferSize);
        
        qint64 bytesToWrite = std::min(static_cast<qint64>(bufferSize), bytesToFill - bytesWrittenTotal);
        qint64 written = wipeFile.write(buffer.data(), bytesToWrite);
        
        if (written <= 0) break; // Disk full or error
        bytesWrittenTotal += written;
        
        if (progressCallback) {
            int progress = static_cast<int>((bytesWrittenTotal * 100) / bytesToFill);
            progressCallback(progress);
        }
    }
    
    // Flush
    HANDLE hFile = reinterpret_cast<HANDLE>(_get_osfhandle(wipeFile.handle()));
    if (hFile != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(hFile);
    }
    
    wipeFile.close();
    
    // The wipe file is already filled with zeros, so overwriting it again (via shredFile) is highly redundant
    // and causes massive UI freezing. We just need to obfuscate the name and delete it.
    QString obfuscatedPath;
    if (renameAndObfuscate(wipeFilePath, obfuscatedPath)) {
        std::wstring wPath = obfuscatedPath.toStdWString();
        if (!DeleteFileW(wPath.c_str())) {
            QFile::remove(obfuscatedPath);
        }
    } else {
        std::wstring wPath = wipeFilePath.toStdWString();
        if (!DeleteFileW(wPath.c_str())) {
            QFile::remove(wipeFilePath);
        }
    }
    
    return !cancelled;
}

bool FileShredder::isDriveSSD(const QString& path) {
    if (path.length() < 2) return false;
    
    // Extract drive letter, e.g. "C:"
    QString driveStr = path.left(2);
    if (!driveStr.endsWith(":")) {
        // Not a standard drive letter path, can't easily detect
        return false; 
    }
    
    QString volumePath = "\\\\.\\" + driveStr;
    HANDLE hDevice = CreateFileW(
        volumePath.toStdWString().c_str(),
        0, // No access rights required to query properties
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );
    
    if (hDevice == INVALID_HANDLE_VALUE) {
        return false;
    }
    
    STORAGE_PROPERTY_QUERY query;
    memset(&query, 0, sizeof(query));
    query.PropertyId = StorageDeviceSeekPenaltyProperty;
    query.QueryType = PropertyStandardQuery;
    
    DEVICE_SEEK_PENALTY_DESCRIPTOR result;
    memset(&result, 0, sizeof(result));
    DWORD bytesReturned = 0;
    
    bool isSSD = false;
    if (DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY,
                        &query, sizeof(query),
                        &result, sizeof(result),
                        &bytesReturned, NULL)) {
        // If there is no seek penalty, it's typically an SSD
        if (!result.IncursSeekPenalty) {
            isSSD = true;
        }
    }
    
    CloseHandle(hDevice);
    return isSSD;
}

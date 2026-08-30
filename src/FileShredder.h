#pragma once
#include <QString>
#include <vector>
#include <functional>

enum class ShredPass {
    Pass_1 = 1,
    Pass_3 = 3, // DoD 5220.22-M
    Pass_7 = 7  // Simplified Gutmann
};

class FileShredder {
public:
    // Shreds a single file using the specified number of passes
    // progressCallback returns progress (0-100) and current status message
    static bool shredFile(const QString& filePath, ShredPass passes, std::function<void(int, const QString&)> progressCallback = nullptr, std::function<bool()> cancelCheck = nullptr);

    // Shreds a file or recursively shreds a folder. Returns the number of failed files (0 = success, -1 = error/not found).
    static int shredPath(const QString& path, ShredPass passes, std::function<void(int, const QString&)> progressCallback = nullptr, std::function<bool()> cancelCheck = nullptr);

    // Wipes free space on the given drive letter (e.g., "C:\\")
    static bool wipeFreeSpace(const QString& drivePath, std::function<void(int)> progressCallback = nullptr, std::function<bool()> cancelCheck = nullptr);

    // Checks if the drive containing the given path is an SSD
    static bool isDriveSSD(const QString& path);

private:
    static bool overwriteFile(const QString& filePath, ShredPass passes, std::function<void(int, const QString&)> progressCallback, std::function<bool()> cancelCheck);
    static bool renameAndObfuscate(const QString& filePath, QString& outNewFilePath);
    static QString generateRandomString(int length);
    static void generateRandomData(char* buffer, size_t size);
};

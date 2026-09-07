#pragma once
#include <string>
#include <vector>
#include <functional>

enum class ShredPass {
    Pass_0 = 0, // Fast delete, no overwrite (MFT/table only)
    Pass_1 = 1,
    Pass_3 = 3, // DoD 5220.22-M
    Pass_7 = 7  // Simplified Gutmann
};

enum class WipeMode {
    FullWipe = 0,
    MftOnly = 1
};

class FileShredder {
public:
    // Shreds a single file using the specified number of passes
    // progressCallback returns progress (0-100) and current status message
    static bool shredFile(const std::wstring& filePath, ShredPass passes, std::function<void(int, const std::wstring&)> progressCallback = nullptr, std::function<bool()> cancelCheck = nullptr);

    // Shreds a file or recursively shreds a folder. Returns the number of failed files (0 = success, -1 = error/not found).
    static int shredPath(const std::wstring& path, ShredPass passes, std::function<void(int, const std::wstring&)> progressCallback = nullptr, std::function<bool()> cancelCheck = nullptr);

    // Wipes free space on the given drive letter (e.g., "C:\\")
    static bool wipeFreeSpace(const std::wstring& drivePath, WipeMode mode, std::function<void(int)> progressCallback = nullptr, std::function<bool()> cancelCheck = nullptr);

    // Checks if the drive containing the given path is an SSD
    static bool isDriveSSD(const std::wstring& path);

private:
    static bool overwriteFile(const std::wstring& filePath, ShredPass passes, std::function<void(int, const std::wstring&)> progressCallback, std::function<bool()> cancelCheck);
    static bool renameAndObfuscate(const std::wstring& filePath, std::wstring& outNewFilePath);
    static std::wstring generateRandomString(int length);
    static void generateRandomData(char* buffer, size_t size);
};

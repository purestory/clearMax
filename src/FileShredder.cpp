#include "FileShredder.h"
#include <random>
#include <fstream>
#define NOMINMAX
#include <windows.h>
#include <io.h>
#include <filesystem>
#include <winioctl.h>
#include <algorithm>
#include <iostream>
#include <chrono>

namespace fs = std::filesystem;

// Generate random string for file name obfuscation
std::wstring FileShredder::generateRandomString(int length) {
    const std::wstring possibleCharacters = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    const int randomStringLength = length;
    std::wstring randomString;
    
    std::random_device rd;
    std::mt19937 generator(rd());
    std::uniform_int_distribution<int> distribution(0, possibleCharacters.length() - 1);
    
    for (int i = 0; i < randomStringLength; ++i) {
        int index = distribution(generator);
        randomString.push_back(possibleCharacters[index]);
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

bool FileShredder::overwriteFile(const std::wstring& filePath, ShredPass passes, std::function<void(int, const std::wstring&)> progressCallback, std::function<bool()> cancelCheck) {
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    
    LARGE_INTEGER liSize;
    if (!GetFileSizeEx(hFile, &liSize)) {
        CloseHandle(hFile);
        return false;
    }
    
    int64_t fileSize = liSize.QuadPart;
    if (fileSize == 0) {
        CloseHandle(hFile);
        return true;
    }
    
    const int numPasses = static_cast<int>(passes);
    if (numPasses == 0) {
        CloseHandle(hFile);
        if (progressCallback) {
            progressCallback(100, filePath);
        }
        return true;
    }
    
    const size_t bufferSize = 1024 * 1024; // 1MB buffer
    std::vector<char> buffer(bufferSize);
    
    for (int p = 0; p < numPasses; ++p) {
        SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
        int64_t bytesWrittenTotal = 0;
        
        while (bytesWrittenTotal < fileSize) {
            if (cancelCheck && cancelCheck()) {
                CloseHandle(hFile);
                return false;
            }
            int64_t bytesToWrite = std::min(static_cast<int64_t>(bufferSize), fileSize - bytesWrittenTotal);
            
            if (numPasses == 1) {
                std::fill(buffer.begin(), buffer.begin() + bytesToWrite, 0x00);
            } else {
                generateRandomData(buffer.data(), bytesToWrite);
            }
            
            DWORD written = 0;
            if (!WriteFile(hFile, buffer.data(), static_cast<DWORD>(bytesToWrite), &written, NULL) || written == 0) {
                CloseHandle(hFile);
                return false; 
            }
            bytesWrittenTotal += written;
            
            if (progressCallback) {
                int overallProgress = static_cast<int>((((p * fileSize) + bytesWrittenTotal) * 100) / (numPasses * fileSize));
                progressCallback(overallProgress, filePath);
            }
        }
        FlushFileBuffers(hFile);
    }
    
    CloseHandle(hFile);
    return true;
}

bool FileShredder::renameAndObfuscate(const std::wstring& filePath, std::wstring& outNewFilePath) {
    std::error_code ec;
    if (!fs::exists(filePath, ec)) return false;
    
    fs::path p(filePath);
    fs::path dir = p.parent_path();
    std::wstring originalName = p.filename().wstring();
    
    // Step 1: Rename to a random string of the SAME length to overwrite the entire MFT filename attribute
    std::wstring sameLenName = generateRandomString(static_cast<int>(originalName.length()));
    fs::path tempPath = dir / sameLenName;
    
    fs::rename(p, tempPath, ec);
    if (ec) {
        tempPath = p; // Fallback to original if first rename fails
    }
    
    // Step 2: Rename to a short random name
    std::wstring newName = generateRandomString(12) + L".tmp";
    outNewFilePath = (dir / newName).wstring();
    
    fs::rename(tempPath, outNewFilePath, ec);
    if (!ec) {
        return true;
    }
    
    outNewFilePath = tempPath.wstring();
    return false;
}

bool FileShredder::shredFile(const std::wstring& filePath, ShredPass passes, std::function<void(int, const std::wstring&)> progressCallback, std::function<bool()> cancelCheck) {
    std::error_code ec;
    if (!fs::exists(filePath, ec)) return false;
    
    // Ensure file is not read-only
    SetFileAttributesW(filePath.c_str(), FILE_ATTRIBUTE_NORMAL);
    
    // Step 1: Overwrite content
    if (!overwriteFile(filePath, passes, progressCallback, cancelCheck)) {
        return false;
    }
    
    // Step 2 & 3: Rename to random name and extension
    std::wstring obfuscatedPath;
    if (!renameAndObfuscate(filePath, obfuscatedPath)) {
        obfuscatedPath = filePath; 
    }
    
    // Step 4: Final deletion using Windows API
    if (DeleteFileW(obfuscatedPath.c_str())) {
        return true;
    }
    
    return fs::remove(obfuscatedPath, ec);
}

int FileShredder::shredPath(const std::wstring& path, ShredPass passes, std::function<void(int, const std::wstring&)> progressCallback, std::function<bool()> cancelCheck) {
    std::error_code ec;
    if (!fs::exists(path, ec)) return -1;
    
    if (fs::is_regular_file(path, ec)) {
        return shredFile(path, passes, progressCallback, cancelCheck) ? 0 : 1;
    } else if (fs::is_directory(path, ec)) {
        std::vector<std::wstring> files;
        int64_t totalSize = 0;
        int gatherCount = 0;
        
        for (const auto& entry : fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied, ec)) {
            if (entry.is_regular_file(ec)) {
                files.push_back(entry.path().wstring());
                totalSize += entry.file_size(ec);
                
                if (++gatherCount % 1000 == 0 && progressCallback) {
                    progressCallback(0, L"Gathering files... (" + std::to_wstring(gatherCount) + L" found)");
                }
            }
        }
        
        int64_t processedSize = 0;
        int failedCount = 0;
        int totalFiles = static_cast<int>(files.size());
        int completedFiles = 0;
        
        for (const std::wstring& file : files) {
            if (cancelCheck && cancelCheck()) return -1;
            
            int64_t fileSize = fs::file_size(file, ec);
            bool ok = shredFile(file, passes, [&](int fileProgress, const std::wstring& currentFile) {
                if (progressCallback) {
                    int64_t currentProcessed = processedSize + (fileSize * fileProgress / 100);
                    int overall = totalSize > 0 ? static_cast<int>((currentProcessed * 100) / totalSize) : 100;
                    
                    std::wstring statusStr = L"[" + std::to_wstring(completedFiles + 1) + L"/" + std::to_wstring(totalFiles) + L"] " + currentFile;
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
                std::wstring statusStr = L"[" + std::to_wstring(completedFiles) + L"/" + std::to_wstring(totalFiles) + L"] " + file;
                progressCallback(overall, statusStr);
            }
        }
        
        // Remove empty directories (reverse iterate to delete deepest first)
        std::vector<std::wstring> dirs;
        for (const auto& entry : fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied, ec)) {
            if (entry.is_directory(ec)) {
                dirs.push_back(entry.path().wstring());
            }
        }
        
        std::sort(dirs.begin(), dirs.end(), [](const std::wstring& a, const std::wstring& b) {
            return a.length() > b.length();
        });
        
        for (const std::wstring& dir : dirs) {
            fs::remove(dir, ec);
        }
        fs::remove(path, ec);
        
        if (progressCallback) {
            progressCallback(100, L"");
        }
        return failedCount;
    }
    
    return -1;
}

bool FileShredder::wipeFreeSpace(const std::wstring& drivePath, WipeMode mode, std::function<void(int)> progressCallback, std::function<bool()> cancelCheck) {
    std::wstring wDrive = drivePath;
    
    ULARGE_INTEGER freeBytesAvailable, totalNumberOfBytes, totalNumberOfFreeBytes;
    if (!GetDiskFreeSpaceExW(wDrive.c_str(), &freeBytesAvailable, &totalNumberOfBytes, &totalNumberOfFreeBytes)) {
        return false;
    }
    
    int64_t bytesToFill = freeBytesAvailable.QuadPart;
    if (bytesToFill <= 0) return true;
    
    std::wstring tempDirStr = drivePath;
    if (!tempDirStr.empty() && tempDirStr.back() != L'\\' && tempDirStr.back() != L'/') {
        tempDirStr += L"\\";
    }

    if (mode == WipeMode::FullWipe) {
        std::wstring wipeFilePath = tempDirStr + L"clearmax_wipe_" + generateRandomString(8) + L".tmp";
        
        HANDLE hWipeFile = CreateFileW(wipeFilePath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hWipeFile == INVALID_HANDLE_VALUE) {
            return false;
        }
        
        const size_t bufferSize = 10 * 1024 * 1024; // 10MB chunk
        std::vector<char> buffer(bufferSize);
        
        int64_t bytesWrittenTotal = 0;
        bool cancelled = false;
        while (bytesWrittenTotal < bytesToFill) {
            if (cancelCheck && cancelCheck()) {
                cancelled = true;
                break;
            }
            
            FileShredder::generateRandomData(buffer.data(), bufferSize);
            
            int64_t bytesToWrite = std::min(static_cast<int64_t>(bufferSize), bytesToFill - bytesWrittenTotal);
            DWORD written = 0;
            if (!WriteFile(hWipeFile, buffer.data(), static_cast<DWORD>(bytesToWrite), &written, NULL) || written == 0) {
                break; 
            }
            
            bytesWrittenTotal += written;
            
            if (progressCallback) {
                int progress = static_cast<int>((bytesWrittenTotal * 90) / bytesToFill);
                progressCallback(progress);
            }
        }
        
        FlushFileBuffers(hWipeFile);
        CloseHandle(hWipeFile);
        
        std::wstring obfuscatedPath;
        if (renameAndObfuscate(wipeFilePath, obfuscatedPath)) {
            if (!DeleteFileW(obfuscatedPath.c_str())) {
                std::error_code ec;
                fs::remove(obfuscatedPath, ec);
            }
        } else {
            if (!DeleteFileW(wipeFilePath.c_str())) {
                std::error_code ec;
                fs::remove(wipeFilePath, ec);
            }
        }
        if (cancelled) return false;
    }

    // --- MFT Wipe Phase ---
    int baseCreationProgress = mode == WipeMode::MftOnly ? 0 : 90;
    int creationProgressRange = mode == WipeMode::MftOnly ? 50 : 5;
    
    if (progressCallback) progressCallback(baseCreationProgress);

    // Use \\?\ prefix to bypass MAX_PATH limits and guarantee dummy file creation
    std::wstring mftDir = L"\\\\?\\" + tempDirStr + L"clearmax_mft_" + generateRandomString(8);
    std::error_code ec;
    fs::create_directories(mftDir, ec);
    
    int mftRecordsToWipe = 300000;
    DWORD bytesReturned;
    NTFS_VOLUME_DATA_BUFFER ntfsVolData;
    std::wstring volPath = L"\\\\.\\" + wDrive.substr(0, 2);
    HANDLE hVol = CreateFileW(volPath.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    
    int64_t initialMftSize = 0;
    int64_t expandedMftSize = 0;
    bool hasExpandedOnce = false;
    
    if (hVol != INVALID_HANDLE_VALUE) {
        if (DeviceIoControl(hVol, FSCTL_GET_NTFS_VOLUME_DATA, NULL, 0, &ntfsVolData, sizeof(ntfsVolData), &bytesReturned, NULL)) {
            initialMftSize = ntfsVolData.MftValidDataLength.QuadPart;
            
            int64_t totalMftRecords = initialMftSize / 1024;
            mftRecordsToWipe = static_cast<int>(totalMftRecords); 
            if (mftRecordsToWipe > 500000) mftRecordsToWipe = 500000; // High cap
            if (mftRecordsToWipe < 50000) mftRecordsToWipe = 50000;
        }
    }
    
    std::wstring currentMftDir = mftDir;
    std::vector<std::wstring> createdDirs;
    auto mftStartTime = std::chrono::steady_clock::now();
    
    for (int i = 0; i < mftRecordsToWipe; ++i) {
        if (cancelCheck && cancelCheck()) break;
        
        if (i % 20000 == 0) {
            currentMftDir = mftDir + L"\\" + std::to_wstring(i);
            fs::create_directories(currentMftDir, ec);
            createdDirs.push_back(currentMftDir);
        }
        
        // Use a very long filename to completely overwrite old filenames in recycled MFT records
        std::wstring dummyFile = currentMftDir + L"\\" + generateRandomString(200) + L".tmp";
        HANDLE hdFile = CreateFileW(dummyFile.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hdFile != INVALID_HANDLE_VALUE) {
            CloseHandle(hdFile);
        }
        
        if (i > 0 && i % 2000 == 0) {
            // MFT 용량 동적 모니터링: 1차 팽창은 허용하여 마저 채워넣고, 2차 팽창 시도 시 즉시 중단
            if (hVol != INVALID_HANDLE_VALUE && initialMftSize > 0) {
                if (DeviceIoControl(hVol, FSCTL_GET_NTFS_VOLUME_DATA, NULL, 0, &ntfsVolData, sizeof(ntfsVolData), &bytesReturned, NULL)) {
                    int64_t currentMftSize = ntfsVolData.MftValidDataLength.QuadPart;
                    
                    if (!hasExpandedOnce) {
                        if (currentMftSize > initialMftSize) {
                            // 1차 팽창 감지: 멈추지 않고 팽창된 공간까지 마저 채우기 위해 기록만 함
                            hasExpandedOnce = true;
                            expandedMftSize = currentMftSize;
                        }
                    } else {
                        if (currentMftSize > expandedMftSize) {
                            // 2차 팽창 감지: 1차 팽창된 공간과 예전 빈 공간이 100% 다 채워졌다는 뜻이므로 즉시 중단
                            mftRecordsToWipe = i;
                            break;
                        }
                    }
                }
            }
            
            if (progressCallback) {
                int progress = baseCreationProgress + (i * creationProgressRange) / mftRecordsToWipe;
                progressCallback(progress);
            }
            
            // Time limit check: max 20 seconds of aggressive MFT wiping per drive to avoid UI hangs
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - mftStartTime).count() >= 20) {
                break;
            }
        }
    }
    
    if (hVol != INVALID_HANDLE_VALUE) {
        CloseHandle(hVol);
    }
    
    int baseDeletionProgress = mode == WipeMode::MftOnly ? 50 : 95;
    int deletionProgressRange = mode == WipeMode::MftOnly ? 50 : 5;
    
    int deletedCount = 0;
    for (const std::wstring& subDir : createdDirs) {
        if (cancelCheck && cancelCheck()) break;
        for (const auto& entry : fs::directory_iterator(subDir, ec)) {
            if (entry.is_regular_file(ec)) {
                fs::remove(entry.path(), ec);
                deletedCount++;
                
                if (deletedCount % 2000 == 0 && progressCallback) {
                    int progress = baseDeletionProgress + (deletedCount * deletionProgressRange) / mftRecordsToWipe;
                    if (progress >= 100) progress = 99; 
                    progressCallback(progress);
                }
            }
        }
        fs::remove(subDir, ec);
    }
    fs::remove(mftDir, ec);

    if (progressCallback) {
        progressCallback(100);
    }
    return true;
}

bool FileShredder::isDriveSSD(const std::wstring& path) {
    if (path.length() < 2) return false;
    
    std::wstring driveStr = path.substr(0, 2);
    if (driveStr.back() != L':') {
        return false; 
    }
    
    std::wstring volumePath = L"\\\\.\\" + driveStr;
    HANDLE hDevice = CreateFileW(
        volumePath.c_str(),
        0, 
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
        if (!result.IncursSeekPenalty) {
            isSSD = true;
        }
    }
    
    CloseHandle(hDevice);
    return isSSD;
}

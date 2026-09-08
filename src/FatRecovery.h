#pragma once
#include <string>
#include <vector>
#include <windows.h>
#include <functional>
#include "RecoveryTypes.h"

class FatRecovery {
public:
    FatRecovery();
    ~FatRecovery();

    // Scan a drive (e.g. L"C:\\") and return a list of deleted files.
    // Progress callback provides 0-100 progress and status string.
    bool scanDrive(const std::wstring& drivePath, std::vector<RecoverableFile>& outFiles, std::function<void(int, const std::wstring&)> progressCallback = nullptr, std::function<bool()> cancelCheck = nullptr);
    
    // Recover a file to a destination path
    bool recoverFile(const std::wstring& drivePath, const RecoverableFile& file, const std::wstring& destPath);

private:
    HANDLE m_hDrive;
    DWORD m_bytesPerSector;
    DWORD m_sectorsPerCluster;
    DWORD m_bytesPerCluster;
    
    // FAT32 Specific
    uint32_t m_fatStartSector;
    uint32_t m_fatSizeSectors;
    uint32_t m_dataStartSector;
    uint32_t m_rootCluster;
    uint32_t m_totalClusters;
    
    bool openDrive(const std::wstring& drivePath);
    void closeDrive();
    bool readBootSector();
    bool readRaw(int64_t offset, DWORD size, void* buffer);
    
    // Recursively scan directories for deleted files
    void scanDirectory(uint32_t cluster, const std::wstring& currentPath, std::vector<RecoverableFile>& outFiles, std::function<void(int, const std::wstring&)> progressCallback, std::function<bool()> cancelCheck);
    
    // Read cluster chain (useful for directory reading)
    bool readClusterChain(uint32_t startCluster, std::vector<uint8_t>& outData, std::function<bool()> cancelCheck = nullptr);
    
    // Convert FAT32 directory entry to our structure
    std::wstring parseShortName(const uint8_t* name);
    
    uint32_t getFatEntry(uint32_t cluster, std::function<bool()> cancelCheck);
};

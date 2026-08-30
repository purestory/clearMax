#pragma once
#include <QString>
#include <QList>
#include <windows.h>
#include <functional>
#include "RecoveryTypes.h"

class FatRecovery {
public:
    FatRecovery();
    ~FatRecovery();

    // Scan a drive (e.g. "C:\\") and return a list of deleted files.
    // Progress callback provides 0-100 progress and status string.
    bool scanDrive(const QString& drivePath, QList<RecoverableFile>& outFiles, std::function<void(int, const QString&)> progressCallback = nullptr);
    
    // Recover a file to a destination path
    bool recoverFile(const QString& drivePath, const RecoverableFile& file, const QString& destPath);

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
    
    bool openDrive(const QString& drivePath);
    void closeDrive();
    bool readBootSector();
    bool readRaw(qint64 offset, DWORD size, void* buffer);
    
    // Recursively scan directories for deleted files
    void scanDirectory(uint32_t cluster, const QString& currentPath, QList<RecoverableFile>& outFiles, std::function<void(int, const QString&)> progressCallback);
    
    // Read cluster chain (useful for directory reading)
    bool readClusterChain(uint32_t startCluster, std::vector<uint8_t>& outData);
    
    // Convert FAT32 directory entry to our structure
    QString parseShortName(const uint8_t* name);
    
    uint32_t getFatEntry(uint32_t cluster);
};

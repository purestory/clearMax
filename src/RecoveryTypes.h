#pragma once
#include <QString>
#include <vector>

struct RecoverableFile {
    QString name;
    QString extension;
    QString fullPath; 
    qint64 size = 0;
    QString recoverability; // "High", "Low", "Overwritten"
    bool isDir = false;
    qint64 recordNumber = 0;       // Used by NTFS (MFT record) and FAT (Directory Entry Sector)
    qint64 parentRecordNumber = 0;
    
    // Internal data for NTFS
    bool isResident = false;
    std::vector<uint8_t> residentData;
    std::vector<std::pair<qint64, qint64>> dataRuns; // <LCN, ClusterCount>
    
    // Internal data for FAT32 / exFAT
    uint32_t fatStartCluster = 0;
};

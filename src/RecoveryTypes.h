#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct RecoverableFile {
    std::wstring name;
    std::wstring extension;
    std::wstring fullPath; 
    int64_t size = 0;
    std::wstring recoverability; // L"High", L"Low", L"Overwritten"
    bool isDir = false;
    int64_t recordNumber = 0;       // Used by NTFS (MFT record) and FAT (Directory Entry Sector)
    int64_t parentRecordNumber = 0;
    
    // Internal data for NTFS
    bool isResident = false;
    std::vector<uint8_t> residentData;
    std::vector<std::pair<int64_t, int64_t>> dataRuns; // <LCN, ClusterCount>
    
    // Internal data for FAT32 / exFAT
    uint32_t fatStartCluster = 0;
};

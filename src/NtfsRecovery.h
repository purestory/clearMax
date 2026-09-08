#pragma once
#include <string>
#include <vector>
#include <windows.h>
#include <memory>
#include <functional>

#include "RecoveryTypes.h"

class NtfsRecovery {
public:
    NtfsRecovery();
    ~NtfsRecovery();

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
    int64_t m_mftStartLCN;
    DWORD m_mftRecordSize;
    
    bool openDrive(const std::wstring& drivePath);
    void closeDrive();
    bool readBootSector();
    bool readRaw(int64_t offset, DWORD size, void* buffer);
    bool parseMFTRecord(uint8_t* recordBuf, int64_t recordNum, RecoverableFile& outFile, bool& inUse);
};

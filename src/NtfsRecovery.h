#pragma once
#include <QString>
#include <QList>
#include <QDateTime>
#include <windows.h>
#include <vector>
#include <memory>
#include <functional>

#include "RecoveryTypes.h"

class NtfsRecovery {
public:
    NtfsRecovery();
    ~NtfsRecovery();

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
    qint64 m_mftStartLCN;
    DWORD m_mftRecordSize;
    
    bool openDrive(const QString& drivePath);
    void closeDrive();
    bool readBootSector();
    bool readRaw(qint64 offset, DWORD size, void* buffer);
    bool parseMFTRecord(uint8_t* recordBuf, qint64 recordNum, RecoverableFile& outFile, bool& inUse);
};

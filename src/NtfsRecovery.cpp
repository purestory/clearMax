#include "NtfsRecovery.h"
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <winioctl.h>
#include <iostream>

#pragma pack(push, 1)
struct NTFS_BOOT_SECTOR {
    uint8_t jump[3];
    char oemID[8];
    uint16_t bytesPerSector;
    uint8_t sectorsPerCluster;
    uint8_t reserved[7];
    uint8_t mediaDescriptor;
    uint16_t zeros;
    uint16_t sectorsPerTrack;
    uint16_t numberOfHeads;
    uint32_t hiddenSectors;
    uint32_t reserved2;
    uint32_t reserved3;
    uint64_t totalSectors;
    uint64_t mftStartLCN;
    uint64_t mftMirrStartLCN;
    int8_t clustersPerMftRecord;
    uint8_t reserved4[3];
    int8_t clustersPerIndexRecord;
    uint8_t reserved5[3];
    uint64_t serialNumber;
    uint32_t checksum;
    uint8_t bootCode[426];
    uint16_t bootSignature;
};

struct MFT_RECORD_HEADER {
    char signature[4]; // "FILE"
    uint16_t updateSequenceOffset;
    uint16_t updateSequenceSize;
    uint64_t logFileSequenceNumber;
    uint16_t sequenceNumber;
    uint16_t hardLinkCount;
    uint16_t firstAttributeOffset;
    uint16_t flags; // 0x01 = In Use, 0x02 = Directory
    uint32_t usedSize;
    uint32_t allocatedSize;
    uint64_t baseRecordReference;
    uint16_t nextAttributeId;
    uint16_t align;
    uint32_t mftRecordNumber;
};

struct ATTRIBUTE_HEADER {
    uint32_t type;
    uint32_t length;
    uint8_t nonResident;
    uint8_t nameLength;
    uint16_t nameOffset;
    uint16_t flags;
    uint16_t attributeId;
    union {
        struct { // Resident
            uint32_t length;
            uint16_t offset;
            uint8_t indexed;
            uint8_t padding;
        } resident;
        struct { // Non-Resident
            uint64_t startVcn;
            uint64_t lastVcn;
            uint16_t runListOffset;
            uint16_t compressionUnit;
            uint32_t padding;
            uint64_t allocatedSize;
            uint64_t realSize;
            uint64_t initializedSize;
        } nonRes;
    };
};

struct FILE_NAME_ATTRIBUTE {
    uint64_t parentDirectory;
    uint64_t creationTime;
    uint64_t alterationTime;
    uint64_t mftChangeTime;
    uint64_t readTime;
    uint64_t allocatedSize;
    uint64_t realSize;
    uint32_t flags;
    uint32_t er;
    uint8_t nameLength;
    uint8_t nameType;
    wchar_t name[1];
};
#pragma pack(pop)

#define ATTR_STANDARD_INFORMATION 0x10
#define ATTR_ATTRIBUTE_LIST 0x20
#define ATTR_FILE_NAME 0x30
#define ATTR_OBJECT_ID 0x40
#define ATTR_SECURITY_DESCRIPTOR 0x50
#define ATTR_VOLUME_NAME 0x60
#define ATTR_VOLUME_INFORMATION 0x70
#define ATTR_DATA 0x80
#define ATTR_INDEX_ROOT 0x90
#define ATTR_INDEX_ALLOCATION 0xA0
#define ATTR_BITMAP 0xB0
#define ATTR_REPARSE_POINT 0xC0

NtfsRecovery::NtfsRecovery() : m_hDrive(INVALID_HANDLE_VALUE), m_bytesPerSector(0), m_sectorsPerCluster(0), m_bytesPerCluster(0), m_mftStartLCN(0), m_mftRecordSize(0) {}

NtfsRecovery::~NtfsRecovery() {
    closeDrive();
}

bool NtfsRecovery::openDrive(const QString& drivePath) {
    if (m_hDrive != INVALID_HANDLE_VALUE) return true;
    
    QString driveLetter = drivePath.left(2);
    QString volumePath = "\\\\.\\" + driveLetter;
    
    m_hDrive = CreateFileW(
        volumePath.toStdWString().c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL
    );
    
    return m_hDrive != INVALID_HANDLE_VALUE;
}

void NtfsRecovery::closeDrive() {
    if (m_hDrive != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hDrive);
        m_hDrive = INVALID_HANDLE_VALUE;
    }
}

bool NtfsRecovery::readRaw(qint64 offset, DWORD size, void* buffer) {
    LARGE_INTEGER li;
    li.QuadPart = offset;
    if (!SetFilePointerEx(m_hDrive, li, NULL, FILE_BEGIN)) return false;
    
    DWORD bytesRead;
    if (!ReadFile(m_hDrive, buffer, size, &bytesRead, NULL)) return false;
    return bytesRead == size;
}

bool NtfsRecovery::readBootSector() {
    NTFS_BOOT_SECTOR boot;
    if (!readRaw(0, sizeof(boot), &boot)) return false;
    
    if (memcmp(boot.oemID, "NTFS    ", 8) != 0) return false;
    
    m_bytesPerSector = boot.bytesPerSector;
    m_sectorsPerCluster = boot.sectorsPerCluster;
    m_bytesPerCluster = m_bytesPerSector * m_sectorsPerCluster;
    m_mftStartLCN = boot.mftStartLCN;
    
    if (boot.clustersPerMftRecord > 0) {
        m_mftRecordSize = boot.clustersPerMftRecord * m_bytesPerCluster;
    } else {
        m_mftRecordSize = 1 << (-boot.clustersPerMftRecord);
    }
    
    return true;
}

bool NtfsRecovery::parseMFTRecord(uint8_t* recordBuf, qint64 recordNum, RecoverableFile& outFile, bool& inUse) {
    MFT_RECORD_HEADER* header = reinterpret_cast<MFT_RECORD_HEADER*>(recordBuf);
    
    if (memcmp(header->signature, "FILE", 4) != 0) return false;
    
    // Apply Update Sequence Array (Fixup)
    if (header->updateSequenceOffset > 0 && header->updateSequenceSize > 0) {
        uint16_t* usa = reinterpret_cast<uint16_t*>(recordBuf + header->updateSequenceOffset);
        uint16_t usn = usa[0];
        
        int numSectors = m_mftRecordSize / m_bytesPerSector;
        if (header->updateSequenceSize >= numSectors + 1) {
            for (int i = 0; i < numSectors; i++) {
                uint16_t* sectorEnd = reinterpret_cast<uint16_t*>(recordBuf + (i + 1) * m_bytesPerSector - 2);
                if (*sectorEnd == usn) {
                    *sectorEnd = usa[i + 1]; // Restore original bytes
                } else {
                    // Record might be corrupt, but we can try to proceed anyway
                }
            }
        }
    }
    
    inUse = (header->flags & 0x01);
    
    outFile.recordNumber = recordNum;
    outFile.isDir = (header->flags & 0x02);
    
    uint32_t offset = header->firstAttributeOffset;
    bool hasName = false;
    bool hasData = false;
    
    while (offset < header->usedSize) {
        ATTRIBUTE_HEADER* attr = reinterpret_cast<ATTRIBUTE_HEADER*>(recordBuf + offset);
        if (attr->type == 0xFFFFFFFF) break; // End of attributes
        
        if (attr->type == ATTR_FILE_NAME) {
            FILE_NAME_ATTRIBUTE* fn = reinterpret_cast<FILE_NAME_ATTRIBUTE*>(recordBuf + offset + attr->resident.offset);
            // DOS name type is 2, skip to get Win32 name (1) or POSIX (0) or Win32+DOS (3)
            if (fn->nameType != 2 || !hasName) { 
                QString name = QString::fromWCharArray(fn->name, fn->nameLength);
                outFile.name = name;
                outFile.parentRecordNumber = fn->parentDirectory & 0x0000FFFFFFFFFFFF;
                int dotIndex = name.lastIndexOf('.');
                if (dotIndex != -1) {
                    outFile.extension = name.mid(dotIndex + 1);
                }
                hasName = true;
            }
        } else if (attr->type == ATTR_DATA && attr->nameLength == 0) { // Default data stream
            hasData = true;
            if (attr->nonResident == 0) { // Resident
                outFile.isResident = true;
                outFile.size = attr->resident.length;
                uint8_t* dataPtr = recordBuf + offset + attr->resident.offset;
                outFile.residentData.assign(dataPtr, dataPtr + outFile.size);
            } else { // Non-Resident
                outFile.isResident = false;
                outFile.size = attr->nonRes.realSize;
                
                // Parse runlist
                uint8_t* runList = recordBuf + offset + attr->nonRes.runListOffset;
                qint64 currentLCN = 0;
                while (*runList != 0x00) {
                    uint8_t lenLength = (*runList) & 0x0F;
                    uint8_t offsetLength = ((*runList) & 0xF0) >> 4;
                    runList++;
                    
                    qint64 length = 0;
                    for (int i = 0; i < lenLength; i++) {
                        length |= static_cast<qint64>(*runList++) << (i * 8);
                    }
                    
                    qint64 offsetChange = 0;
                    for (int i = 0; i < offsetLength; i++) {
                        offsetChange |= static_cast<qint64>(*runList++) << (i * 8);
                    }
                    // Sign extend offsetChange
                    if (offsetLength > 0 && (offsetChange & (1ULL << (offsetLength * 8 - 1)))) {
                        for (int i = offsetLength; i < 8; i++) {
                            offsetChange |= (0xFFULL << (i * 8));
                        }
                    }
                    
                    currentLCN += offsetChange;
                    outFile.dataRuns.push_back({currentLCN, length});
                }
            }
        }
        
        offset += attr->length;
        if (attr->length == 0) break; // Prevent infinite loop
    }
    
    if (hasName) {
        outFile.recoverability = "High";
        if (!outFile.isResident && outFile.dataRuns.empty() && outFile.size > 0) {
            outFile.recoverability = "Low";
        }
        return true;
    }
    
    return false;
}

bool NtfsRecovery::scanDrive(const QString& drivePath, QList<RecoverableFile>& outFiles, std::function<void(int, const QString&)> progressCallback) {
    if (!openDrive(drivePath)) return false;
    if (!readBootSector()) return false;
    
    outFiles.clear();
    
    qint64 mftByteOffset = m_mftStartLCN * m_bytesPerCluster;
    std::vector<uint8_t> recordBuf(m_mftRecordSize);
    
    int maxRecordsToScan = 150000; // Limit for performance in this simple version
    
    QHash<qint64, std::pair<QString, qint64>> dirMap; // recordNumber -> <Name, ParentRecordNumber>
    QList<RecoverableFile> tempDeletedFiles;
    
    for (int i = 0; i < maxRecordsToScan; ++i) {
        if (!readRaw(mftByteOffset + (i * m_mftRecordSize), m_mftRecordSize, recordBuf.data())) break;
        
        // Quick check for FILE signature
        if (memcmp(recordBuf.data(), "FILE", 4) == 0) {
            RecoverableFile rf;
            bool inUse = false;
            if (parseMFTRecord(recordBuf.data(), i, rf, inUse)) {
                if (rf.isDir) {
                    dirMap.insert(i, {rf.name, rf.parentRecordNumber});
                }
                if (!inUse) {
                    tempDeletedFiles.append(rf);
                }
            }
        }
        
        if (progressCallback && i % 5000 == 0) {
            int progress = (i * 100) / maxRecordsToScan;
            progressCallback(progress, QString("Scanning MFT Record %1... Found %2 deleted files").arg(i).arg(tempDeletedFiles.size()));
        }
    }
    
    // Resolve full paths
    for (RecoverableFile& rf : tempDeletedFiles) {
        QStringList pathParts;
        qint64 currentParent = rf.parentRecordNumber;
        int maxDepth = 20; // Prevent infinite loops
        while (currentParent != 5 && currentParent != 0 && maxDepth-- > 0) { // 5 is root dir
            if (dirMap.contains(currentParent)) {
                auto dirInfo = dirMap.value(currentParent);
                pathParts.prepend(dirInfo.first);
                if (currentParent == dirInfo.second) break; // self-referencing check
                currentParent = dirInfo.second;
            } else {
                pathParts.prepend(QString("Folder_%1").arg(currentParent));
                pathParts.prepend("Unknown Folders");
                break;
            }
        }
        if (pathParts.isEmpty()) {
            rf.fullPath = "Root";
        } else {
            rf.fullPath = pathParts.join("/");
        }
        outFiles.append(rf);
    }
    
    if (progressCallback) progressCallback(100, QString("Scan complete. Found %1 deleted files.").arg(outFiles.size()));
    
    return true;
}

bool NtfsRecovery::recoverFile(const QString& drivePath, const RecoverableFile& file, const QString& destPath) {
    if (file.isDir) return false;
    
    if (!openDrive(drivePath)) return false;
    if (!readBootSector()) return false;
    
    QFile destFile(destPath);
    if (!destFile.open(QIODevice::WriteOnly)) return false;
    
    if (file.isResident) {
        destFile.write(reinterpret_cast<const char*>(file.residentData.data()), file.residentData.size());
        return true;
    } else {
        qint64 remainingSize = file.size;
        std::vector<char> clusterBuf(m_bytesPerCluster);
        
        for (const auto& run : file.dataRuns) {
            qint64 lcn = run.first;
            qint64 count = run.second;
            
            for (qint64 i = 0; i < count && remainingSize > 0; ++i) {
                qint64 byteOffset = (lcn + i) * m_bytesPerCluster;
                if (!readRaw(byteOffset, m_bytesPerCluster, clusterBuf.data())) return false;
                
                qint64 toWrite = qMin(static_cast<qint64>(m_bytesPerCluster), remainingSize);
                destFile.write(clusterBuf.data(), toWrite);
                remainingSize -= toWrite;
            }
        }
        return true;
    }
}

#include "FatRecovery.h"
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <winioctl.h>
#include <iostream>

#pragma pack(push, 1)
struct FAT32_BOOT_SECTOR {
    uint8_t jump[3];
    char oemID[8];
    uint16_t bytesPerSector;
    uint8_t sectorsPerCluster;
    uint16_t reservedSectors;
    uint8_t numberOfFATs;
    uint16_t rootEntries;
    uint16_t totalSectors16;
    uint8_t mediaDescriptor;
    uint16_t fatSize16;
    uint16_t sectorsPerTrack;
    uint16_t numberOfHeads;
    uint32_t hiddenSectors;
    uint32_t totalSectors32;
    uint32_t fatSize32;
    uint16_t extFlags;
    uint16_t fsVersion;
    uint32_t rootCluster;
    uint16_t fsInfoSector;
    uint16_t backupBootSector;
    uint8_t reserved[12];
    uint8_t driveNumber;
    uint8_t reserved1;
    uint8_t bootSignature;
    uint32_t volumeID;
    char volumeLabel[11];
    char fileSystemType[8];
    uint8_t bootCode[420];
    uint16_t bootSectorSignature;
};

struct FAT_DIR_ENTRY {
    uint8_t name[11];
    uint8_t attr;
    uint8_t ntRes;
    uint8_t crtTimeTenth;
    uint16_t crtTime;
    uint16_t crtDate;
    uint16_t lstAccDate;
    uint16_t fstClusHI;
    uint16_t wrtTime;
    uint16_t wrtDate;
    uint16_t fstClusLO;
    uint32_t fileSize;
};
#pragma pack(pop)

FatRecovery::FatRecovery() : m_hDrive(INVALID_HANDLE_VALUE), m_bytesPerSector(0), m_sectorsPerCluster(0), m_bytesPerCluster(0) {}

FatRecovery::~FatRecovery() {
    closeDrive();
}

bool FatRecovery::openDrive(const QString& drivePath) {
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

void FatRecovery::closeDrive() {
    if (m_hDrive != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hDrive);
        m_hDrive = INVALID_HANDLE_VALUE;
    }
}

bool FatRecovery::readRaw(qint64 offset, DWORD size, void* buffer) {
    LARGE_INTEGER li;
    li.QuadPart = offset;
    if (!SetFilePointerEx(m_hDrive, li, NULL, FILE_BEGIN)) return false;
    
    DWORD bytesRead;
    if (!ReadFile(m_hDrive, buffer, size, &bytesRead, NULL)) return false;
    return bytesRead == size;
}

bool FatRecovery::readBootSector() {
    FAT32_BOOT_SECTOR boot;
    if (!readRaw(0, sizeof(boot), &boot)) return false;
    
    if (boot.bootSectorSignature != 0xAA55) return false;
    
    m_bytesPerSector = boot.bytesPerSector;
    m_sectorsPerCluster = boot.sectorsPerCluster;
    if (m_bytesPerSector == 0 || m_sectorsPerCluster == 0) return false;
    
    m_bytesPerCluster = m_bytesPerSector * m_sectorsPerCluster;
    
    m_fatStartSector = boot.reservedSectors;
    m_fatSizeSectors = boot.fatSize32 != 0 ? boot.fatSize32 : boot.fatSize16;
    m_dataStartSector = m_fatStartSector + (boot.numberOfFATs * m_fatSizeSectors);
    m_rootCluster = boot.rootCluster;
    
    uint32_t totalSectors = boot.totalSectors32 != 0 ? boot.totalSectors32 : boot.totalSectors16;
    m_totalClusters = (totalSectors - m_dataStartSector) / m_sectorsPerCluster;
    
    return true;
}

uint32_t FatRecovery::getFatEntry(uint32_t cluster) {
    if (cluster < 2 || cluster >= m_totalClusters + 2) return 0x0FFFFFFF;
    uint32_t fatOffset = cluster * 4;
    uint32_t sector = m_fatStartSector + (fatOffset / m_bytesPerSector);
    uint32_t offsetInSector = fatOffset % m_bytesPerSector;
    
    std::vector<uint8_t> sectorBuf(m_bytesPerSector);
    if (!readRaw(static_cast<qint64>(sector) * m_bytesPerSector, m_bytesPerSector, sectorBuf.data())) {
        return 0x0FFFFFFF;
    }
    
    uint32_t nextCluster = *reinterpret_cast<uint32_t*>(sectorBuf.data() + offsetInSector);
    return nextCluster & 0x0FFFFFFF;
}

bool FatRecovery::readClusterChain(uint32_t startCluster, std::vector<uint8_t>& outData) {
    uint32_t cluster = startCluster;
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        qint64 offset = static_cast<qint64>(m_dataStartSector + (cluster - 2) * m_sectorsPerCluster) * m_bytesPerSector;
        std::vector<uint8_t> clusterBuf(m_bytesPerCluster);
        if (!readRaw(offset, m_bytesPerCluster, clusterBuf.data())) break;
        outData.insert(outData.end(), clusterBuf.begin(), clusterBuf.end());
        cluster = getFatEntry(cluster);
    }
    return !outData.empty();
}

QString FatRecovery::parseShortName(const uint8_t* name) {
    QString res;
    // Name is 8 chars, ext is 3 chars. First char might be 0xE5
    for (int i = 0; i < 8; i++) {
        if (name[i] == 0x20) break;
        if (i == 0 && name[i] == 0xE5) res += "_";
        else res += QChar(name[i]);
    }
    QString ext;
    for (int i = 8; i < 11; i++) {
        if (name[i] == 0x20) break;
        ext += QChar(name[i]);
    }
    if (!ext.isEmpty()) res += "." + ext;
    return res;
}

void FatRecovery::scanDirectory(uint32_t startCluster, const QString& currentPath, QList<RecoverableFile>& outFiles, std::function<void(int, const QString&)> progressCallback) {
    std::vector<uint8_t> dirData;
    if (!readClusterChain(startCluster, dirData)) return;
    
    size_t numEntries = dirData.size() / sizeof(FAT_DIR_ENTRY);
    FAT_DIR_ENTRY* entries = reinterpret_cast<FAT_DIR_ENTRY*>(dirData.data());
    
    for (size_t i = 0; i < numEntries; i++) {
        FAT_DIR_ENTRY& entry = entries[i];
        
        if (entry.name[0] == 0x00) break; // End of directory
        if (entry.name[0] == 0xE5) { // Deleted file
            if (entry.attr & 0x0F) continue; // Skip LFN or Volume ID for deleted files
            
            RecoverableFile rf;
            rf.name = parseShortName(entry.name);
            int dotIndex = rf.name.lastIndexOf('.');
            if (dotIndex != -1) rf.extension = rf.name.mid(dotIndex + 1);
            
            rf.fullPath = currentPath;
            rf.size = entry.fileSize;
            rf.isDir = (entry.attr & 0x10) != 0;
            rf.fatStartCluster = (static_cast<uint32_t>(entry.fstClusHI) << 16) | entry.fstClusLO;
            
            // Check recoverability (very basic check for FAT: if start cluster is free in FAT table, it might be recoverable, but FAT chain is lost.
            // If the start cluster is marked free, we have to assume contiguous clusters.
            uint32_t fatVal = getFatEntry(rf.fatStartCluster);
            if (fatVal == 0) {
                rf.recoverability = "Low"; // Chain is lost, contiguous recovery only
            } else if (fatVal >= 0x0FFFFFF8) {
                rf.recoverability = "High"; // Small file fitting in one cluster
            } else {
                rf.recoverability = "Overwritten"; // Cluster reused by another file
            }
            
            outFiles.append(rf);
        } else if (entry.name[0] != 0x05 && (entry.attr & 0x10) && !(entry.attr & 0x08)) {
            // Valid sub-directory, recurse
            // Avoid . and ..
            if (entry.name[0] != '.') {
                QString dirName = parseShortName(entry.name);
                uint32_t subCluster = (static_cast<uint32_t>(entry.fstClusHI) << 16) | entry.fstClusLO;
                if (subCluster != 0 && subCluster != startCluster) {
                    scanDirectory(subCluster, currentPath + "/" + dirName, outFiles, progressCallback);
                }
            }
        }
    }
}

bool FatRecovery::scanDrive(const QString& drivePath, QList<RecoverableFile>& outFiles, std::function<void(int, const QString&)> progressCallback) {
    if (!openDrive(drivePath)) return false;
    
    if (progressCallback) progressCallback(5, "Reading Boot Sector...");
    if (!readBootSector()) {
        closeDrive();
        return false;
    }
    
    if (progressCallback) progressCallback(10, "Scanning FAT Directories...");
    
    scanDirectory(m_rootCluster, "", outFiles, progressCallback);
    
    if (progressCallback) progressCallback(100, "Scan Complete");
    closeDrive();
    return true;
}

bool FatRecovery::recoverFile(const QString& drivePath, const RecoverableFile& file, const QString& destPath) {
    if (!openDrive(drivePath)) return false;
    if (!readBootSector()) {
        closeDrive();
        return false;
    }
    
    QFile outFile(destPath);
    if (!outFile.open(QIODevice::WriteOnly)) {
        closeDrive();
        return false;
    }
    
    // Recovery for FAT is tricky because the FAT chain is cleared upon deletion.
    // We assume the file was contiguous on disk.
    uint32_t cluster = file.fatStartCluster;
    qint64 remaining = file.size;
    
    std::vector<uint8_t> clusterBuf(m_bytesPerCluster);
    while (remaining > 0 && cluster >= 2 && cluster < m_totalClusters + 2) {
        qint64 offset = static_cast<qint64>(m_dataStartSector + (cluster - 2) * m_sectorsPerCluster) * m_bytesPerSector;
        if (!readRaw(offset, m_bytesPerCluster, clusterBuf.data())) break;
        
        qint64 toWrite = (std::min)(remaining, static_cast<qint64>(m_bytesPerCluster));
        outFile.write(reinterpret_cast<char*>(clusterBuf.data()), toWrite);
        remaining -= toWrite;
        
        cluster++; // Assume contiguous
    }
    
    outFile.close();
    closeDrive();
    return remaining == 0;
}

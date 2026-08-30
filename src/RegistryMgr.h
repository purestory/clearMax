#pragma once
#include <QString>
#include <QList>
#include <windows.h>

struct ProgramInfo {
    QString displayName;
    QString displayVersion;
    QString publisher;
    QString uninstallString;
    QString registryKeyPath;
    QString displayIcon; // Path to icon
    QString installDate; // YYYYMMDD format
    DWORD estimatedSize; // Size in KB
    bool isGhost; // true if files don't exist but registry remains
};

class RegistryMgr {
public:
    static QList<ProgramInfo> getInstalledPrograms();
    
    // Normal uninstall via UninstallString
    static bool uninstallProgram(const ProgramInfo& info);
    
    // Force remove registry key and attempt to shred remaining files in install location
    static bool forceRemoveProgram(const ProgramInfo& info);

private:
    static void scanRegistryKey(HKEY rootKey, const QString& subKey, QList<ProgramInfo>& programs);
    static QString getRegString(HKEY key, const QString& valueName);
    static DWORD getRegDword(HKEY key, const QString& valueName);
};

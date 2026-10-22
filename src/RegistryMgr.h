#pragma once
#include <string>
#include <vector>
#include <windows.h>
#include <functional>

struct ProgramInfo {
    std::wstring displayName;
    std::wstring displayVersion;
    std::wstring publisher;
    std::wstring uninstallString;
    std::wstring registryKeyPath;
    std::wstring displayIcon; // Path to icon
    int iconIndex = -1; // Index in the image list
    std::wstring installDate; // YYYYMMDD format
    DWORD estimatedSize; // Size in KB
    bool isGhost; // true if files don't exist but registry remains
    std::wstring installLocation;
    std::wstring lastUsedDate;
};

class RegistryMgr {
public:
    static std::vector<ProgramInfo> getInstalledPrograms();
    
    // Normal uninstall via UninstallString
    static bool uninstallProgram(const ProgramInfo& info, std::function<void()> onFinished = nullptr);
    
    // Force remove registry key and attempt to shred remaining files in install location
    static bool forceRemoveProgram(const ProgramInfo& info, bool& rebootRequired, bool& needSafeMode, std::wstring& outResolvedPath, std::vector<std::wstring>& outServices);
    static bool forceRemoveProgram(const ProgramInfo& info, bool& rebootRequired);

    // Schedule 1-shot Safe Mode purge and auto-reboot to normal mode
    static bool scheduleSafeModePurge(const std::wstring& targetDir, const std::vector<std::wstring>& serviceNames);
    static int RunSafePurgeService();

    static std::wstring fetchLastUsedDate(const std::wstring& installLocation, const std::wstring& uninstallString);

    static bool isContextMenuEnabled();
    static bool setContextMenuEnabled(bool enable);

private:
    static void scanRegistryKey(HKEY rootKey, const std::wstring& subKey, std::vector<ProgramInfo>& programs);
    static std::wstring getRegString(HKEY key, const std::wstring& valueName);
    static DWORD getRegDword(HKEY key, const std::wstring& valueName);
};

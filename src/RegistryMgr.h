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
};

class RegistryMgr {
public:
    static std::vector<ProgramInfo> getInstalledPrograms();
    
    // Normal uninstall via UninstallString
    static bool uninstallProgram(const ProgramInfo& info, std::function<void()> onFinished = nullptr);
    
    // Force remove registry key and attempt to shred remaining files in install location
    static bool forceRemoveProgram(const ProgramInfo& info);

private:
    static void scanRegistryKey(HKEY rootKey, const std::wstring& subKey, std::vector<ProgramInfo>& programs);
    static std::wstring getRegString(HKEY key, const std::wstring& valueName);
    static DWORD getRegDword(HKEY key, const std::wstring& valueName);
};

#pragma once
#include <string>
#include <vector>
#include <windows.h>

struct StartupItem {
    std::wstring name;
    std::wstring command;
    std::wstring location;
    bool isEnabled;
    std::wstring regKeyPath;
    std::wstring regValueName;
    HKEY regRoot;
    bool isFolderShortcut;
    std::wstring shortcutPath;
    int csidl = 0;
    
    // Scheduled Task specific
    bool isScheduledTask = false;
    std::wstring taskPath;
    
    int iconIndex = -1;
};

class StartupMgr {
public:
    static std::vector<StartupItem> getStartupItems();
    static bool setStartupItemState(StartupItem& item, bool enable);
    static bool deleteStartupItem(const StartupItem& item);
};

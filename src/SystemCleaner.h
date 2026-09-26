#pragma once
#include <string>
#include <vector>
#include <functional>
#include "FileShredder.h"

class SystemCleaner {
public:
    static void cleanTempFiles(ShredPass passes, std::function<void(int)> progressCallback = nullptr);
    static void cleanPrefetch(ShredPass passes, std::function<void(int)> progressCallback = nullptr);
    static void emptyRecycleBin(ShredPass passes, std::function<void(int)> progressCallback = nullptr);
    static void cleanSystemLogs(ShredPass passes, std::function<void(int)> progressCallback = nullptr);
};

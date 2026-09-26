#include "SpaceAnalyzer.h"
#include <windows.h>
#include <algorithm>

SpaceAnalyzer::SpaceAnalyzer() : m_scannedFiles(0), m_lastTimeMs(0) {}

SpaceAnalyzer::~SpaceAnalyzer() {}

void SpaceAnalyzer::scanPath(const std::wstring& path, std::function<void(int, const std::wstring&)> progressCallback, std::function<bool()> cancelCheck) {
    m_scannedFiles = 0;
    m_lastTimeMs = GetTickCount64();
    
    // Normalize path (ensure no trailing slash unless it's a root like C:\)
    std::wstring rootName = path;
    if (rootName.length() > 3 && (rootName.back() == L'\\' || rootName.back() == L'/')) {
        rootName.pop_back();
    }
    
    m_root = std::make_unique<DirNode>(rootName, true, nullptr);
    
    scanRecursive(m_root.get(), rootName, progressCallback, cancelCheck);
}

void SpaceAnalyzer::scanRecursive(DirNode* node, const std::wstring& path, std::function<void(int, const std::wstring&)>& progressCallback, std::function<bool()>& cancelCheck) {
    if (cancelCheck && cancelCheck()) return;

    std::wstring searchPath = path;
    if (searchPath.back() != L'\\') searchPath += L"\\";
    searchPath += L"*";

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    std::vector<std::pair<DirNode*, std::wstring>> dirsToScan;

    do {
        if (cancelCheck && cancelCheck()) break;
        
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;

        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        
        auto child = std::make_unique<DirNode>(fd.cFileName, isDir, node);
        DirNode* childPtr = child.get();
        
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            node->children.push_back(std::move(child));
        }
        
        if (isDir) {
            std::wstring subPath = path;
            if (subPath.back() != L'\\') subPath += L"\\";
            subPath += fd.cFileName;
            dirsToScan.push_back({childPtr, subPath});
        } else {
            uint64_t fileSize = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
            childPtr->size = fileSize;
            childPtr->count = 1;
            
            node->addSize(fileSize, 1);
            m_scannedFiles++;
        }
        
        uint64_t now = GetTickCount64();
        if (now - m_lastTimeMs > 100) {
            m_lastTimeMs = now;
            if (progressCallback) {
                progressCallback(0, path);
            }
        }
        
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);

    for (auto& dir : dirsToScan) {
        if (cancelCheck && cancelCheck()) break;
        scanRecursive(dir.first, dir.second, progressCallback, cancelCheck);
    }
}

std::vector<DirNode*> SpaceAnalyzer::getSortedChildren(DirNode* node, SortMode mode) {
    std::vector<DirNode*> result;
    if (!node) return result;
    
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& child : node->children) {
            result.push_back(child.get());
        }
    }
    
    std::sort(result.begin(), result.end(), [mode](DirNode* a, DirNode* b) {
        if (a->isDir != b->isDir) return a->isDir; // Folders first
        if (mode == SortMode::SizeDesc) {
            return a->size > b->size;
        } else {
            return _wcsicmp(a->name.c_str(), b->name.c_str()) < 0;
        }
    });
    return result;
}

void SpaceAnalyzer::removeNode(DirNode* node) {
    if (!node || !node->parent) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    node->parent->removeSize(node->size, node->count);
    auto& siblings = node->parent->children;
    siblings.erase(std::remove_if(siblings.begin(), siblings.end(),
        [node](const std::unique_ptr<DirNode>& child) { return child.get() == node; }), siblings.end());
}

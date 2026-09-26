#pragma once
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <functional>
#include <mutex>

struct DirNode {
    std::wstring name;
    bool isDir;
    std::atomic<uint64_t> size;
    std::atomic<uint32_t> count;
    DirNode* parent;
    std::vector<std::unique_ptr<DirNode>> children;

    DirNode(const std::wstring& n, bool d, DirNode* p) : name(n), isDir(d), size(0), count(0), parent(p) {}
    
    void addSize(uint64_t s, uint32_t c) {
        size += s;
        count += c;
        if (parent) parent->addSize(s, c);
    }
    
    void removeSize(uint64_t s, uint32_t c) {
        size -= s;
        count -= c;
        if (parent) parent->removeSize(s, c);
    }
};

class SpaceAnalyzer {
public:
    SpaceAnalyzer();
    ~SpaceAnalyzer();

    enum class SortMode {
        SizeDesc,
        NameAsc
    };

    void scanPath(const std::wstring& path, std::function<void(int, const std::wstring&)> progressCallback, std::function<bool()> cancelCheck);
    
    DirNode* getRoot() { return m_root.get(); }
    
    std::vector<DirNode*> getSortedChildren(DirNode* node, SortMode mode = SortMode::SizeDesc);
    
    void removeNode(DirNode* node);
    
    std::mutex& getMutex() { return m_mutex; }

private:
    std::unique_ptr<DirNode> m_root;
    std::mutex m_mutex;
    uint64_t m_scannedFiles;
    uint64_t m_lastTimeMs;
    std::wstring m_currentPath;
    
    void scanRecursive(DirNode* node, const std::wstring& path, std::function<void(int, const std::wstring&)>& progressCallback, std::function<bool()>& cancelCheck);
};

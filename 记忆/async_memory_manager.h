// async_memory_manager.h
#pragma once
#include "memory_manager.h"
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <future>

class AsyncMemoryManager : public MemoryManager {
public:
    AsyncMemoryManager();
    ~AsyncMemoryManager();
    
    // 重写父类方法，添加异步支持
    void addMemory(const std::string& content, MemoryType type, MemoryCategory category) override;
    void save() override;
    
    // 新增异步方法
    void asyncSave();
    void flushPendingWrites();
    bool isSaving() const { return isSavingFlag.load(); }
    
private:
    // 异步保存相关
    std::queue<MemoryItem> pendingWrites;
    std::mutex pendingMutex;
    std::condition_variable saveCondition;
    std::thread saveThread;
    std::atomic<bool> shouldStop{false};
    std::atomic<bool> isSavingFlag{false};
    
    // 批量保存配置
    static const size_t BATCH_SIZE = 100;
    static const std::chrono::milliseconds SAVE_INTERVAL{5000}; // 5秒自动保存
    
    // 内部方法
    void saveWorker();
    void saveBatch(const std::vector<MemoryItem>& batch);
    std::vector<MemoryItem> getPendingBatch();
};

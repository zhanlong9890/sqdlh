// async_memory_manager.cpp
#include "async_memory_manager.h"
#include "logger.h"
#include <fstream>
#include <chrono>
#include <algorithm>

AsyncMemoryManager::AsyncMemoryManager() : MemoryManager() {
    // 启动异步保存线程
    saveThread = std::thread(&AsyncMemoryManager::saveWorker, this);
    Logger::log("AsyncMemoryManager initialized with background save thread");
}

AsyncMemoryManager::~AsyncMemoryManager() {
    // 停止保存线程
    shouldStop.store(true);
    saveCondition.notify_all();
    if (saveThread.joinable()) {
        saveThread.join();
    }
    
    // 保存剩余数据
    flushPendingWrites();
    Logger::log("AsyncMemoryManager destroyed");
}

void AsyncMemoryManager::addMemory(const std::string& content, MemoryType type, MemoryCategory category) {
    // 调用父类方法添加记忆
    MemoryManager::addMemory(content, type, category);
    
    // 添加到待保存队列
    time_t now = time(nullptr);
    MemoryItem item{content, type, category, std::to_string(now)};
    
    {
        std::lock_guard<std::mutex> lock(pendingMutex);
        pendingWrites.push(item);
    }
    
    // 通知保存线程
    saveCondition.notify_one();
    
    // 如果队列过大，立即保存
    if (pendingWrites.size() >= BATCH_SIZE) {
        asyncSave();
    }
}

void AsyncMemoryManager::save() {
    // 同步保存：立即保存所有待写入数据
    flushPendingWrites();
    MemoryManager::save();
}

void AsyncMemoryManager::asyncSave() {
    // 异步保存：触发后台保存
    saveCondition.notify_one();
}

void AsyncMemoryManager::flushPendingWrites() {
    std::lock_guard<std::mutex> lock(pendingMutex);
    if (pendingWrites.empty()) return;
    
    std::vector<MemoryItem> batch;
    while (!pendingWrites.empty() && batch.size() < BATCH_SIZE) {
        batch.push_back(pendingWrites.front());
        pendingWrites.pop();
    }
    
    if (!batch.empty()) {
        saveBatch(batch);
    }
}

void AsyncMemoryManager::saveWorker() {
    while (!shouldStop.load()) {
        std::unique_lock<std::mutex> lock(pendingMutex);
        
        // 等待保存条件或超时
        saveCondition.wait_for(lock, SAVE_INTERVAL, [this] {
            return !pendingWrites.empty() || shouldStop.load();
        });
        
        if (shouldStop.load()) break;
        
        // 获取待保存批次
        std::vector<MemoryItem> batch = getPendingBatch();
        lock.unlock();
        
        if (!batch.empty()) {
            isSavingFlag.store(true);
            saveBatch(batch);
            isSavingFlag.store(false);
        }
    }
}

std::vector<MemoryItem> AsyncMemoryManager::getPendingBatch() {
    std::vector<MemoryItem> batch;
    while (!pendingWrites.empty() && batch.size() < BATCH_SIZE) {
        batch.push_back(pendingWrites.front());
        pendingWrites.pop();
    }
    return batch;
}

void AsyncMemoryManager::saveBatch(const std::vector<MemoryItem>& batch) {
    try {
        // 按类型分组保存
        std::ofstream sfile(SHORT_PATH, std::ios::app);
        std::ofstream mfile(MID_PATH, std::ios::app);
        std::ofstream lfile(LONG_PATH, std::ios::app);
        
        for (const auto& item : batch) {
            std::string line = item.content + "|" + 
                             std::to_string(static_cast<int>(item.category)) + "|" + 
                             item.timestamp + "\n";
            
            switch (item.type) {
                case MemoryType::Short:
                    sfile << line;
                    break;
                case MemoryType::Mid:
                    mfile << line;
                    break;
                case MemoryType::Long:
                    lfile << line;
                    break;
            }
        }
        
        Logger::log("Batch saved: " + std::to_string(batch.size()) + " items");
    } catch (const std::exception& e) {
        Logger::log("Batch save failed: " + std::string(e.what()), "ERROR");
    }
}

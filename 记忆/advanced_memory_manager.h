// advanced_memory_manager.h
#pragma once
#include "async_memory_manager.h"
#include "semantic_search.h"
#include "weight_manager.h"
#include "event_system.h"
#include "lru_cache.h"
#include <memory>
#include <thread>
#include <atomic>

class AdvancedMemoryManager {
public:
    AdvancedMemoryManager();
    ~AdvancedMemoryManager();
    
    // 核心功能
    void addMemory(const std::string& content, MemoryType type, MemoryCategory category);
    std::vector<MemoryItem> searchMemories(const std::string& query, size_t maxResults = 10);
    std::vector<MemoryItem> getRecentMemories(size_t count = 50);
    std::vector<MemoryItem> getTopMemories(size_t count = 10);
    
    // 高级功能
    void updateMemoryWeight(const std::string& content, float weight);
    void recordMemoryAccess(const std::string& content);
    void cleanupExpiredMemories();
    
    // 批量操作
    void addMemoriesBatch(const std::vector<std::pair<std::string, MemoryType>>& memories);
    std::vector<MemoryItem> searchMemoriesBatch(const std::vector<std::string>& queries, size_t maxResults = 10);
    
    // 统计和监控
    struct SystemStatistics {
        size_t totalMemories;
        size_t cacheHitRate;
        float averageSearchTime;
        size_t totalSearches;
        WeightManager::WeightStatistics weightStats;
        SemanticSearch::Statistics searchStats;
        EventBus::EventStatistics eventStats;
    };
    SystemStatistics getSystemStatistics() const;
    
    // 配置管理
    void setSearchThreshold(float threshold);
    void setCacheSize(size_t size);
    void setWeightConfig(const WeightConfig& config);
    
    // 系统控制
    void start();
    void stop();
    bool isRunning() const { return running_.load(); }
    
private:
    // 核心组件
    std::unique_ptr<AsyncMemoryManager> asyncManager_;
    std::unique_ptr<SemanticSearch> semanticSearch_;
    std::unique_ptr<WeightManager> weightManager_;
    std::unique_ptr<EventManager> eventManager_;
    std::unique_ptr<LRUCache<std::string, MemoryItem>> memoryCache_;
    
    // 配置
    float searchThreshold_;
    size_t cacheSize_;
    WeightConfig weightConfig_;
    
    // 状态管理
    std::atomic<bool> running_{false};
    std::thread backgroundThread_;
    
    // 统计信息
    mutable std::atomic<size_t> totalSearches_{0};
    mutable std::atomic<float> totalSearchTime_{0.0f};
    mutable std::atomic<size_t> cacheHits_{0};
    mutable std::atomic<size_t> cacheMisses_{0};
    
    // 内部方法
    void initializeComponents();
    void setupEventHandlers();
    void backgroundWorker();
    void onMemoryAdded(const Event& event);
    void onMemorySearched(const Event& event);
    void onWeightUpdated(const Event& event);
    MemoryCategory classifyMemory(const std::string& content);
    std::string getCurrentTimestamp() const;
};

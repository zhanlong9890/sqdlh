// advanced_memory_manager.cpp
#include "advanced_memory_manager.h"
#include "logger.h"
#include <chrono>
#include <algorithm>

AdvancedMemoryManager::AdvancedMemoryManager() 
    : searchThreshold_(0.5f), cacheSize_(1000) {
    initializeComponents();
    setupEventHandlers();
    Logger::log("AdvancedMemoryManager initialized");
}

AdvancedMemoryManager::~AdvancedMemoryManager() {
    stop();
    Logger::log("AdvancedMemoryManager destroyed");
}

void AdvancedMemoryManager::initializeComponents() {
    // 初始化异步内存管理器
    asyncManager_ = std::make_unique<AsyncMemoryManager>();
    
    // 初始化权重管理器
    weightManager_ = std::make_unique<WeightManager>(weightConfig_);
    
    // 初始化语义搜索（需要向量模型）
    // semanticSearch_ = std::make_unique<SemanticSearch>(vectorModel, cacheSize_);
    
    // 初始化事件管理器
    eventManager_ = std::make_unique<EventManager>();
    
    // 初始化LRU缓存
    memoryCache_ = std::make_unique<LRUCache<std::string, MemoryItem>>(cacheSize_);
    
    Logger::log("All components initialized");
}

void AdvancedMemoryManager::setupEventHandlers() {
    auto& eventBus = EventBus::getInstance();
    
    // 订阅记忆添加事件
    eventBus.subscribe(EventTypes::MEMORY_ADDED, 
                      [this](const Event& event) { onMemoryAdded(event); });
    
    // 订阅记忆搜索事件
    eventBus.subscribe(EventTypes::MEMORY_SEARCHED, 
                      [this](const Event& event) { onMemorySearched(event); });
    
    // 订阅权重更新事件
    eventBus.subscribe(EventTypes::WEIGHT_UPDATED, 
                      [this](const Event& event) { onWeightUpdated(event); });
    
    Logger::log("Event handlers setup completed");
}

void AdvancedMemoryManager::addMemory(const std::string& content, MemoryType type, MemoryCategory category) {
    if (!running_.load()) {
        Logger::log("System not running, cannot add memory", "WARN");
        return;
    }
    
    // 自动分类
    if (category == MemoryCategory::Other) {
        category = classifyMemory(content);
    }
    
    // 添加到异步管理器
    asyncManager_->addMemory(content, type, category);
    
    // 更新权重
    auto currentTime = getCurrentTimestamp();
    MemoryItem item{content, type, category, currentTime};
    weightManager_->recordAccess(content, currentTime);
    
    // 发布事件
    Event event(EventTypes::MEMORY_ADDED, item);
    EventBus::getInstance().publish(event);
    
    Logger::log("Added memory: " + content);
}

std::vector<MemoryItem> AdvancedMemoryManager::searchMemories(const std::string& query, size_t maxResults) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // 检查缓存
    auto cached = memoryCache_->get(query);
    if (cached) {
        cacheHits_++;
        return {*cached};
    }
    
    cacheMisses_++;
    
    // 语义搜索
    std::vector<MemoryItem> results;
    if (semanticSearch_) {
        auto searchResults = semanticSearch_->search(query, maxResults, searchThreshold_);
        results.reserve(searchResults.size());
        
        for (const auto& result : searchResults) {
            results.push_back(result.memory);
        }
    } else {
        // 回退到简单搜索
        results = asyncManager_->getRelatedMemories(query, maxResults);
    }
    
    // 更新搜索统计
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    totalSearchTime_ += duration.count();
    totalSearches_++;
    
    // 缓存结果
    if (!results.empty()) {
        memoryCache_->put(query, results[0]);
    }
    
    // 发布搜索事件
    Event event(EventTypes::MEMORY_SEARCHED, query);
    EventBus::getInstance().publish(event);
    
    return results;
}

std::vector<MemoryItem> AdvancedMemoryManager::getRecentMemories(size_t count) {
    return asyncManager_->getRecentMemories();
}

std::vector<MemoryItem> AdvancedMemoryManager::getTopMemories(size_t count) {
    return asyncManager_->getTopMemories(count);
}

void AdvancedMemoryManager::updateMemoryWeight(const std::string& content, float weight) {
    weightManager_->updateMemoryWeight(content, weight);
    
    // 发布权重更新事件
    Event event(EventTypes::WEIGHT_UPDATED, std::make_pair(content, weight));
    EventBus::getInstance().publish(event);
}

void AdvancedMemoryManager::recordMemoryAccess(const std::string& content) {
    auto currentTime = getCurrentTimestamp();
    weightManager_->recordAccess(content, currentTime);
}

void AdvancedMemoryManager::cleanupExpiredMemories() {
    if (semanticSearch_) {
        semanticSearch_->cleanupExpiredMemories();
    }
    weightManager_->cleanupExpiredData();
    
    Logger::log("Expired memories cleaned up");
}

void AdvancedMemoryManager::addMemoriesBatch(const std::vector<std::pair<std::string, MemoryType>>& memories) {
    for (const auto& [content, type] : memories) {
        addMemory(content, type, MemoryCategory::Other);
    }
    
    Logger::log("Batch added " + std::to_string(memories.size()) + " memories");
}

std::vector<MemoryItem> AdvancedMemoryManager::searchMemoriesBatch(const std::vector<std::string>& queries, size_t maxResults) {
    std::vector<MemoryItem> allResults;
    
    for (const auto& query : queries) {
        auto results = searchMemories(query, maxResults);
        allResults.insert(allResults.end(), results.begin(), results.end());
    }
    
    // 去重
    std::sort(allResults.begin(), allResults.end(), 
              [](const MemoryItem& a, const MemoryItem& b) {
                  return a.content < b.content;
              });
    
    auto it = std::unique(allResults.begin(), allResults.end(),
                         [](const MemoryItem& a, const MemoryItem& b) {
                             return a.content == b.content;
                         });
    allResults.erase(it, allResults.end());
    
    return allResults;
}

AdvancedMemoryManager::SystemStatistics AdvancedMemoryManager::getSystemStatistics() const {
    SystemStatistics stats;
    
    stats.totalMemories = asyncManager_->getAllMemories().size();
    stats.totalSearches = totalSearches_.load();
    stats.averageSearchTime = totalSearches_.load() > 0 ? 
                             totalSearchTime_.load() / totalSearches_.load() : 0.0f;
    
    // 计算缓存命中率
    size_t totalCacheAccess = cacheHits_.load() + cacheMisses_.load();
    stats.cacheHitRate = totalCacheAccess > 0 ? 
                        (cacheHits_.load() * 100) / totalCacheAccess : 0;
    
    if (weightManager_) {
        stats.weightStats = weightManager_->getStatistics();
    }
    
    if (semanticSearch_) {
        stats.searchStats = semanticSearch_->getStatistics();
    }
    
    stats.eventStats = EventBus::getInstance().getStatistics();
    
    return stats;
}

void AdvancedMemoryManager::setSearchThreshold(float threshold) {
    searchThreshold_ = threshold;
    Logger::log("Search threshold set to: " + std::to_string(threshold));
}

void AdvancedMemoryManager::setCacheSize(size_t size) {
    cacheSize_ = size;
    memoryCache_ = std::make_unique<LRUCache<std::string, MemoryItem>>(size);
    Logger::log("Cache size set to: " + std::to_string(size));
}

void AdvancedMemoryManager::setWeightConfig(const WeightConfig& config) {
    weightConfig_ = config;
    if (weightManager_) {
        weightManager_->setConfig(config);
    }
    Logger::log("Weight configuration updated");
}

void AdvancedMemoryManager::start() {
    if (running_.load()) {
        return;
    }
    
    running_.store(true);
    
    // 启动事件管理器
    if (eventManager_) {
        eventManager_->start();
    }
    
    // 启动后台线程
    backgroundThread_ = std::thread(&AdvancedMemoryManager::backgroundWorker, this);
    
    // 发布系统启动事件
    Event event(EventTypes::SYSTEM_STARTED);
    EventBus::getInstance().publish(event);
    
    Logger::log("AdvancedMemoryManager started");
}

void AdvancedMemoryManager::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    // 停止后台线程
    if (backgroundThread_.joinable()) {
        backgroundThread_.join();
    }
    
    // 停止事件管理器
    if (eventManager_) {
        eventManager_->stop();
    }
    
    // 发布系统停止事件
    Event event(EventTypes::SYSTEM_STOPPED);
    EventBus::getInstance().publish(event);
    
    Logger::log("AdvancedMemoryManager stopped");
}

void AdvancedMemoryManager::backgroundWorker() {
    while (running_.load()) {
        try {
            // 定期清理过期数据
            cleanupExpiredMemories();
            
            // 更新权重
            auto memories = asyncManager_->getAllMemories();
            if (!memories.empty()) {
                weightManager_->updateWeights(memories, getCurrentTimestamp());
            }
            
            // 等待一段时间
            std::this_thread::sleep_for(std::chrono::minutes(5));
        } catch (const std::exception& e) {
            Logger::log("Background worker error: " + std::string(e.what()), "ERROR");
        }
    }
}

void AdvancedMemoryManager::onMemoryAdded(const Event& event) {
    try {
        auto memory = event.getDataAs<MemoryItem>();
        Logger::log("Event: Memory added - " + memory.content);
    } catch (const std::exception& e) {
        Logger::log("Error processing memory added event: " + std::string(e.what()), "ERROR");
    }
}

void AdvancedMemoryManager::onMemorySearched(const Event& event) {
    try {
        auto query = event.getDataAs<std::string>();
        Logger::log("Event: Memory searched - " + query);
    } catch (const std::exception& e) {
        Logger::log("Error processing memory searched event: " + std::string(e.what()), "ERROR");
    }
}

void AdvancedMemoryManager::onWeightUpdated(const Event& event) {
    try {
        auto [content, weight] = event.getDataAs<std::pair<std::string, float>>();
        Logger::log("Event: Weight updated - " + content + " -> " + std::to_string(weight));
    } catch (const std::exception& e) {
        Logger::log("Error processing weight updated event: " + std::string(e.what()), "ERROR");
    }
}

MemoryCategory AdvancedMemoryManager::classifyMemory(const std::string& content) {
    // 简单的关键词分类
    std::string lowerContent = content;
    std::transform(lowerContent.begin(), lowerContent.end(), lowerContent.begin(), ::tolower);
    
    if (lowerContent.find("工作") != std::string::npos || 
        lowerContent.find("项目") != std::string::npos) {
        return MemoryCategory::Work;
    }
    
    if (lowerContent.find("家庭") != std::string::npos || 
        lowerContent.find("父母") != std::string::npos) {
        return MemoryCategory::Family;
    }
    
    if (lowerContent.find("朋友") != std::string::npos || 
        lowerContent.find("聚会") != std::string::npos) {
        return MemoryCategory::Friendship;
    }
    
    if (lowerContent.find("开心") != std::string::npos || 
        lowerContent.find("高兴") != std::string::npos) {
        return MemoryCategory::Happiness;
    }
    
    return MemoryCategory::Other;
}

std::string AdvancedMemoryManager::getCurrentTimestamp() const {
    return std::to_string(std::time(nullptr));
}

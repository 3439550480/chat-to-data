#pragma once
#include <memory>
#include <string>
#include <optional>
#include <vector>
#include <mutex>
#include <odb/mysql/database.hxx>
#include <sw/redis++/redis.h>
#include "worksheetEntity.h"

namespace fileService {

// worksheet映射操作类：MySQL 持久化 + Redis 缓存（TTL 3天）
class WorksheetData {
public:
    WorksheetData(std::shared_ptr<odb::database> db,
                  std::shared_ptr<sw::redis::Redis> redis);
    // ---------- MySQL ----------
    // 保存工作表映射到MySQL
    bool saveWorksheetMappingToDb(const WorksheetEntity& worksheet);
    // 根据fileId从MySQL中获取工作表映射列表
    std::vector<WorksheetEntity> getWorksheetMappingsByFileIdFromDb(const std::string& fileId);
    // 根据fileId从MySQL中删除工作表映射（方案A：业务级级联删除，替代FK）
    bool deleteWorksheetMappingsByFileIdFromDb(const std::string& fileId);
    // ---------- Redis ----------
    // 保存工作表映射列表到Redis缓存（JSON 串，TTL 3天）
    bool saveWorksheetMappingsToCache(const std::string& fileId,
                                      const std::vector<WorksheetEntity>& worksheets);
    // 根据fileId从Redis缓存中获取工作表映射列表
    std::optional<std::vector<WorksheetEntity>> getWorksheetMappingsFromCache(
        const std::string& fileId);
    // 根据fileId从Redis缓存中删除工作表映射
    void deleteWorksheetMappingsCache(const std::string& fileId);
private:
    std::shared_ptr<odb::database> _db;         // MySQL 操作实例
    std::shared_ptr<sw::redis::Redis> _redis;   // Redis 操作实例
    std::mutex _redisMutex;                     // 保护 Redis 操作的互斥锁
    const static int WORKSHEET_CACHE_TTL_SECONDS = 3 * 24 * 60 * 60;  // 缓存过期：3天
};

} // namespace fileService

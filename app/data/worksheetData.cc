#include "worksheetData.h"
#include <bite_scaffold/log.h>
#include <bite_scaffold/util.h>
#include <odb/transaction.hxx>
#include "worksheetEntity.h"
#include "odb/worksheetEntity-odb.hxx"

namespace fileService {

WorksheetData::WorksheetData(std::shared_ptr<odb::database> db,
                             std::shared_ptr<sw::redis::Redis> redis)
    : _db(db), _redis(redis) {
    INF("WorksheetData initialized");
}

// 保存工作表映射到MySQL
bool WorksheetData::saveWorksheetMappingToDb(const WorksheetEntity& worksheet) {
    try {
        // 1. 开启事务
        odb::transaction trans(_db->begin());
        // 2. 保存映射（odb的persist需要非const引用，const_cast 课件同款）
        _db->persist(const_cast<WorksheetEntity&>(worksheet));
        // 3. 提交事务
        trans.commit();
        INF("WorksheetMapping saved to DB: fileId={}, worksheetName={}, tableName={}",
            worksheet.fileId(), worksheet.worksheetName(), worksheet.tableName());
        return true;
    } catch (const std::exception& e) {
        ERR("Failed to save WorksheetMapping to DB: fileId={}, error={}",
            worksheet.fileId(), e.what());
        return false;
    }
}

// 根据fileId从MySQL中获取工作表映射列表
std::vector<WorksheetEntity> WorksheetData::getWorksheetMappingsByFileIdFromDb(
    const std::string& fileId) {
    std::vector<WorksheetEntity> result;
    try {
        odb::transaction trans(_db->begin());
        odb::result<WorksheetEntity> queryResult = _db->query<WorksheetEntity>(
            odb::query<WorksheetEntity>::fileId == fileId);
        for (const auto& worksheet : queryResult) {
            result.push_back(worksheet);
        }
        trans.commit();
        INF("WorksheetMappings found in DB by fileId: fileId={}, count={}",
            fileId, result.size());
        return result;
    } catch (const std::exception& e) {
        ERR("Failed to get WorksheetMappings from DB by fileId: fileId={}, error={}",
            fileId, e.what());
        return result;
    }
}

// 根据fileId从MySQL中删除工作表映射（方案A：业务级级联删除，替代 FK ON DELETE CASCADE）
bool WorksheetData::deleteWorksheetMappingsByFileIdFromDb(const std::string& fileId) {
    try {
        odb::transaction trans(_db->begin());
        _db->erase_query<WorksheetEntity>(
            odb::query<WorksheetEntity>::fileId == fileId);
        trans.commit();
        INF("WorksheetMappings deleted from DB: fileId={}", fileId);
        return true;
    } catch (const std::exception& e) {
        ERR("Failed to delete WorksheetMappings from DB: fileId={}, error={}",
            fileId, e.what());
        return false;
    }
}

// 保存工作表映射列表到Redis缓存
bool WorksheetData::saveWorksheetMappingsToCache(const std::string& fileId,
                                                 const std::vector<WorksheetEntity>& worksheets) {
    try {
        // 1. 缓存键：worksheet:<fileId>
        std::string key = "worksheet:" + fileId;
        // 2. 构建缓存 JSON：{ fileId, worksheets: [ {fileId, worksheetName, tableName} ] }
        Json::Value rootJson;
        rootJson["fileId"] = fileId;
        Json::Value worksheetArray(Json::arrayValue);
        for (const auto& worksheet : worksheets) {
            Json::Value worksheetJson;
            worksheetJson["fileId"] = worksheet.fileId();
            worksheetJson["worksheetName"] = worksheet.worksheetName();
            worksheetJson["tableName"] = worksheet.tableName();
            worksheetArray.append(worksheetJson);
        }
        rootJson["worksheets"] = worksheetArray;
        // 3. 序列化
        auto jsonStr = biteutil::JSON::serialize(rootJson);
        if (!jsonStr.has_value()) {
            WRN("Failed to serialize WorksheetMappings for cache: fileId={}", fileId);
            return false;
        }
        // 4. 写入缓存（TTL 3天）
        {
            std::lock_guard<std::mutex> lock(_redisMutex);
            _redis->setex(key, WORKSHEET_CACHE_TTL_SECONDS, jsonStr.value());
        }
        INF("WorksheetMappings saved to cache: fileId={}", fileId);
        return true;
    } catch (const std::exception& e) {
        ERR("Failed to save WorksheetMappings to cache: fileId={}, error={}",
            fileId, e.what());
        return false;
    }
}

// 根据fileId从Redis缓存中获取工作表映射列表
std::optional<std::vector<WorksheetEntity>> WorksheetData::getWorksheetMappingsFromCache(
    const std::string& fileId) {
    try {
        std::string key = "worksheet:" + fileId;
        // 1. 读缓存
        std::string jsonStr;
        {
            std::lock_guard<std::mutex> lock(_redisMutex);
            auto result = _redis->get(key);
            if (!result.has_value() || result.value().empty()) {
                INF("WorksheetMappings not found in cache by fileId: fileId={}", fileId);
                return std::nullopt;
            }
            jsonStr = result.value();
        }
        // 2. 反序列化
        auto jsonOpt = biteutil::JSON::unserialize(jsonStr);
        if (!jsonOpt.has_value()) {
            WRN("Failed to unserialize WorksheetMappings from cache: fileId={}", fileId);
            return std::nullopt;
        }
        // 3. 提取映射列表
        std::vector<WorksheetEntity> worksheets;
        const auto& worksheetArray = jsonOpt.value()["worksheets"];
        for (const auto& worksheetJson : worksheetArray) {
            WorksheetEntity worksheet;
            worksheet.setFileId(worksheetJson["fileId"].asString());
            worksheet.setWorksheetName(worksheetJson["worksheetName"].asString());
            worksheet.setTableName(worksheetJson["tableName"].asString());
            worksheets.push_back(worksheet);
        }
        INF("WorksheetMappings found in cache by fileId: fileId={}, count={}",
            fileId, worksheets.size());
        return worksheets;
    } catch (const std::exception& e) {
        ERR("Failed to get WorksheetMappings from cache by fileId: fileId={}, error={}",
            fileId, e.what());
        return std::nullopt;
    }
}

// 根据fileId从Redis缓存中删除工作表映射
void WorksheetData::deleteWorksheetMappingsCache(const std::string& fileId) {
    try {
        std::string key = "worksheet:" + fileId;
        std::lock_guard<std::mutex> lock(_redisMutex);
        _redis->del(key);
        INF("WorksheetMappings cache deleted: fileId={}", fileId);
    } catch (const std::exception& e) {
        ERR("Failed to delete WorksheetMappings cache: fileId={}, error={}",
            fileId, e.what());
    }
}

} // namespace fileService

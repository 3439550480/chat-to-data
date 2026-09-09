#pragma once
#include <memory>
#include <string>
#include <optional>
#include <vector>
#include <mutex>
#include <odb/mysql/database.hxx>
#include <sw/redis++/redis.h>
#include "fileEntity.h"

namespace fileService {

// 文件元数据操作类：MySQL 持久化 + Redis 缓存（Cache-Aside，TTL 3天）
class FileInfoData {
public:
    FileInfoData(std::shared_ptr<odb::database> db,
                 std::shared_ptr<sw::redis::Redis> redis);
    // ---------- MySQL ----------
    // 保存或更新文件信息到MySQL（fileId存在则更新，否则插入）
    bool saveFileInfoToDb(const FileInfoEntity& fileInfo);
    // 根据文件ID从MySQL获取文件信息
    std::optional<FileInfoEntity> getFileInfoByFileIdFromDb(const std::string& fileId);
    // 根据文件ID和用户ID从MySQL获取文件信息（归属校验）
    std::optional<FileInfoEntity> getFileInfoByFileIdAndUserIdFromDb(const std::string& fileId,
                                                                     const std::string& userId);
    // 根据文件ID从MySQL删除文件信息
    bool deleteFileInfoByFileIdFromDb(const std::string& fileId);
    // 根据用户ID从MySQL获取用户文件列表
    std::vector<FileInfoEntity> getFileListByUserIdFromDb(const std::string& userId);
    // ---------- Redis ----------
    // 保存文件信息到Redis缓存（JSON 串，TTL 3天）
    bool saveFileInfoToCache(const FileInfoEntity& fileInfo);
    // 根据文件ID从Redis缓存获取文件信息
    std::optional<FileInfoEntity> getFileInfoFromCacheByFileId(const std::string& fileId);
    // 删除Redis缓存中的文件信息
    void deleteFileInfoCache(const std::string& fileId);
private:
    std::shared_ptr<odb::database> _db;            // MySQL 操作实例
    std::shared_ptr<sw::redis::Redis> _redis;      // Redis 操作实例
    std::mutex _redisMutex;                        // 保护 Redis 操作的互斥锁
    const static int FILE_CACHE_TTL_SECONDS = 3 * 24 * 60 * 60;  // 缓存过期：3天
};

} // namespace fileService

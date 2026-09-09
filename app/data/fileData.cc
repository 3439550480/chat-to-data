#include "fileData.h"
#include <bite_scaffold/log.h>
#include <bite_scaffold/util.h>
#include <odb/transaction.hxx>
#include "fileEntity.h"
#include "odb/fileEntity-odb.hxx"

namespace fileService {

FileInfoData::FileInfoData(std::shared_ptr<odb::database> db,
                           std::shared_ptr<sw::redis::Redis> redis)
    : _db(db), _redis(redis) {
    INF("FileInfoData initialized");
}

// 保存或更新文件信息到MySQL数据库
bool FileInfoData::saveFileInfoToDb(const FileInfoEntity& fileInfo) {
    try {
        // 1. 开启事务
        odb::transaction trans(_db->begin());
        // 2. 查询fileId对应的文件是否存在
        auto existingFile = _db->query_one<FileInfoEntity>(
            odb::query<FileInfoEntity>::fileId == fileInfo.fileId());
        // 3. 存在则更新，不存在则插入
        if (existingFile) {
            existingFile->setFileName(fileInfo.fileName());
            existingFile->setFileExt(fileInfo.fileExt());
            existingFile->setFileSize(fileInfo.fileSize());
            existingFile->setUploadTime(fileInfo.uploadTime());
            existingFile->setFdfsFileId(fileInfo.fdfsFileId());
            existingFile->setUserId(fileInfo.userId());
            existingFile->setChatSessionId(fileInfo.chatSessionId());
            _db->update(*existingFile);
            INF("FileInfo updated in DB: fileId={}", fileInfo.fileId());
        } else {
            // 注：odb的persist需要非const引用，使用const_cast（课件同款）
            _db->persist(const_cast<FileInfoEntity&>(fileInfo));
            INF("FileInfo saved to DB: fileId={}", fileInfo.fileId());
        }
        // 4. 提交事务
        trans.commit();
        return true;
    } catch (const std::exception& e) {
        ERR("Failed to save FileInfo to DB: fileId={}, error={}",
            fileInfo.fileId(), e.what());
        return false;
    }
}

// 根据fileId从MySQL数据库中查询文件信息
std::optional<FileInfoEntity> FileInfoData::getFileInfoByFileIdFromDb(const std::string& fileId) {
    try {
        odb::transaction trans(_db->begin());
        auto result = _db->query_one<FileInfoEntity>(
            odb::query<FileInfoEntity>::fileId == fileId);
        trans.commit();
        if (result) {
            INF("FileInfo found in DB by fileId: fileId={}", fileId);
            return *result;
        }
        WRN("FileInfo not found in DB by fileId: fileId={}", fileId);
        return std::nullopt;
    } catch (const std::exception& e) {
        ERR("Failed to get FileInfo from DB by fileId: fileId={}, error={}",
            fileId, e.what());
        return std::nullopt;
    }
}

// 根据fileId+userId查询（归属校验：文件必须属于当前用户）
std::optional<FileInfoEntity> FileInfoData::getFileInfoByFileIdAndUserIdFromDb(
    const std::string& fileId, const std::string& userId) {
    try {
        odb::transaction trans(_db->begin());
        auto result = _db->query_one<FileInfoEntity>(
            (odb::query<FileInfoEntity>::fileId == fileId) &&
            (odb::query<FileInfoEntity>::userId == userId));
        trans.commit();
        if (result) {
            INF("FileInfo found in DB by fileId and userId: fileId={}, userId={}",
                fileId, userId);
            return *result;
        }
        WRN("FileInfo not found in DB by fileId and userId: fileId={}, userId={}",
            fileId, userId);
        return std::nullopt;
    } catch (const std::exception& e) {
        ERR("Failed to get FileInfo from DB by fileId and userId: fileId={}, userId={}, error={}",
            fileId, userId, e.what());
        return std::nullopt;
    }
}

// 根据fileId删除文件信息
bool FileInfoData::deleteFileInfoByFileIdFromDb(const std::string& fileId) {
    try {
        odb::transaction trans(_db->begin());
        _db->erase_query<FileInfoEntity>(
            odb::query<FileInfoEntity>::fileId == fileId);
        trans.commit();
        INF("FileInfo deleted from DB: fileId={}", fileId);
        return true;
    } catch (const std::exception& e) {
        ERR("Failed to delete FileInfo from DB: fileId={}, error={}", fileId, e.what());
        return false;
    }
}

// 根据userId获取用户所有文件信息
std::vector<FileInfoEntity> FileInfoData::getFileListByUserIdFromDb(const std::string& userId) {
    std::vector<FileInfoEntity> result;
    try {
        odb::transaction trans(_db->begin());
        odb::result<FileInfoEntity> queryResult = _db->query<FileInfoEntity>(
            odb::query<FileInfoEntity>::userId == userId);
        for (const auto& file : queryResult) {
            result.push_back(file);
        }
        trans.commit();
        INF("FileList found in DB by userId: userId={}, count={}", userId, result.size());
        return result;
    } catch (const std::exception& e) {
        ERR("Failed to get FileList from DB by userId: userId={}, error={}",
            userId, e.what());
        return result;
    }
}

// 保存文件信息到缓存（JSON 串，TTL 3天）
bool FileInfoData::saveFileInfoToCache(const FileInfoEntity& fileInfo) {
    try {
        // 1. 缓存键：file:<fileId>
        std::string key = "file:" + fileInfo.fileId();
        // 2. 构建缓存值（JSON 串）
        Json::Value fileJson;
        fileJson["fileId"] = fileInfo.fileId();
        fileJson["fileName"] = fileInfo.fileName();
        fileJson["fileExt"] = fileInfo.fileExt();
        fileJson["fileSize"] = static_cast<Json::Int64>(fileInfo.fileSize());
        fileJson["uploadTime"] = static_cast<Json::Int64>(fileInfo.uploadTime());
        fileJson["fdfsFileId"] = fileInfo.fdfsFileId();
        fileJson["userId"] = fileInfo.userId();
        fileJson["chatSessionId"] = fileInfo.chatSessionId();
        // 3. 序列化
        auto jsonStr = biteutil::JSON::serialize(fileJson);
        if (!jsonStr.has_value()) {
            WRN("Failed to serialize FileInfo for cache: fileId={}", fileInfo.fileId());
            return false;
        }
        // 4. 写入缓存（TTL 3天）
        {
            std::lock_guard<std::mutex> lock(_redisMutex);
            _redis->setex(key, FILE_CACHE_TTL_SECONDS, jsonStr.value());
        }
        INF("FileInfo saved to cache: fileId={}", fileInfo.fileId());
        return true;
    } catch (const std::exception& e) {
        ERR("Failed to save FileInfo to cache: fileId={}, error={}",
            fileInfo.fileId(), e.what());
        return false;
    }
}

// 根据fileId从缓存中获取文件信息
std::optional<FileInfoEntity> FileInfoData::getFileInfoFromCacheByFileId(const std::string& fileId) {
    try {
        std::string key = "file:" + fileId;
        // 1. 读缓存
        std::string jsonStr;
        {
            std::lock_guard<std::mutex> lock(_redisMutex);
            auto result = _redis->get(key);
            if (!result.has_value() || result.value().empty()) {
                INF("FileInfo not found in cache by fileId: fileId={}", fileId);
                return std::nullopt;
            }
            jsonStr = result.value();
        }
        // 2. 反序列化
        auto jsonOpt = biteutil::JSON::unserialize(jsonStr);
        if (!jsonOpt.has_value()) {
            WRN("Failed to unserialize FileInfo from cache: fileId={}", fileId);
            return std::nullopt;
        }
        // 3. 提取文件信息
        FileInfoEntity fileInfo;
        fileInfo.setFileId(jsonOpt.value()["fileId"].asString());
        fileInfo.setFileName(jsonOpt.value()["fileName"].asString());
        fileInfo.setFileExt(jsonOpt.value()["fileExt"].asString());
        fileInfo.setFileSize(jsonOpt.value()["fileSize"].asInt64());
        fileInfo.setUploadTime(jsonOpt.value()["uploadTime"].asInt64());
        fileInfo.setFdfsFileId(jsonOpt.value()["fdfsFileId"].asString());
        fileInfo.setUserId(jsonOpt.value()["userId"].asString());
        fileInfo.setChatSessionId(jsonOpt.value()["chatSessionId"].asString());
        INF("FileInfo found in cache by fileId: fileId={}", fileId);
        return fileInfo;
    } catch (const std::exception& e) {
        ERR("Failed to get FileInfo from cache by fileId: fileId={}, error={}",
            fileId, e.what());
        return std::nullopt;
    }
}

// 根据fileId从缓存中删除文件信息
void FileInfoData::deleteFileInfoCache(const std::string& fileId) {
    try {
        std::string key = "file:" + fileId;
        std::lock_guard<std::mutex> lock(_redisMutex);
        _redis->del(key);
        INF("FileInfo cache deleted: fileId={}", fileId);
    } catch (const std::exception& e) {
        ERR("Failed to delete FileInfo cache: fileId={}, error={}", fileId, e.what());
    }
}

} // namespace fileService

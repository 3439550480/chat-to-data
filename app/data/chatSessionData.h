#pragma once

#include <memory>
#include <string>
#include <optional>
#include <vector>
#include <mutex>
#include <odb/mysql/database.hxx>
#include <sw/redis++/redis.h>
#include "chatSessionEntity.h"

namespace aiService {

// 聊天会话元数据的数据层：MySQL（权威数据）+ Redis（Cache-Aside 缓存）
// 注意：聊天消息本体不在本层管理（ChatSDK 的 chatDB.db 负责），这里只管"会话账本"
class ChatSessionData {
public:
    ChatSessionData(std::shared_ptr<odb::database> db,
                    std::shared_ptr<sw::redis::Redis> redis);

    // ---------- MySQL ----------
    // 保存或更新聊天会话（按 chatSessionId 查：存在→逐字段更新；不存在→插入）
    bool saveChatSessionToDb(const ChatSessionEntity& chatSession);
    // 按会话 Id 查询（缓存未命中时的回源路径）
    std::optional<ChatSessionEntity> getChatSessionBySessionIdFromDb(const std::string& chatSessionId);
    // 按用户 Id 查询其全部会话（会话列表；刻意不走缓存——单键缓存按会话存，无法按用户聚合）
    std::vector<ChatSessionEntity> getChatSessionByUserIdFromDb(const std::string& userId);
    // 按会话 Id 删除
    bool deleteChatSessionBySessionIdFromDb(const std::string& chatSessionId);

    // ---------- Redis ----------
    // 会话元数据写入缓存（JSON 整体存入，TTL 3 天——与文件生命周期对齐：
    // 文件删了会话也没必要存在）
    bool saveChatSessionToCache(const ChatSessionEntity& chatSession);
    // 从缓存读会话（键不存在/JSON 损坏 → nullopt，由上层回源 MySQL）
    std::optional<ChatSessionEntity> getChatSessionFromCacheBySessionId(const std::string& chatSessionId);
    // 删除缓存（写 DB 成功后调用——Cache-Aside 写策略）
    void deleteChatSessionFromCache(const std::string& chatSessionId);

private:
    std::shared_ptr<odb::database> _db;         // MySQL（ODB）
    std::shared_ptr<sw::redis::Redis> _redis;   // Redis 客户端
    std::mutex _redisMutex;                     // redis++ 连接非线程安全，所有 Redis 操作持锁
    const static int CHAT_SESSION_CACHE_TTL_SECONDS = 3 * 24 * 60 * 60;   // 3 天
};

} // namespace aiService

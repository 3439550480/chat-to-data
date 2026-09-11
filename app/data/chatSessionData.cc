#include "chatSessionData.h"
#include <bite_scaffold/log.h>
#include <bite_scaffold/util.h>
#include <odb/transaction.hxx>
#include "odb/chatSessionEntity-odb.hxx"

namespace aiService {

ChatSessionData::ChatSessionData(std::shared_ptr<odb::database> db,
                                 std::shared_ptr<sw::redis::Redis> redis)
    : _db(db)
    , _redis(redis) {
    INF("ChatSessionData initialized");
}

// 保存或更新聊天会话到 MySQL（事务：查询→更新/插入→提交）
bool ChatSessionData::saveChatSessionToDb(const ChatSessionEntity& chatSession) {
    try {
        odb::transaction trans(_db->begin());
        auto result = _db->query_one<ChatSessionEntity>(
            odb::query<ChatSessionEntity>::chatSessionId == chatSession.chatSessionId());
        if (result) {
            // 存在 → 逐字段更新（ODB 要求在托管对象上 update）
            result->setUserId(chatSession.userId());
            result->setTitle(chatSession.title());
            result->setCreateTime(chatSession.createTime());
            result->setUpdateTime(chatSession.updateTime());
            result->setMessageCount(chatSession.messageCount());
            result->setModelName(chatSession.modelName());
            result->setFileId(chatSession.fileId());
            result->setSessionType(chatSession.sessionType());
            result->setDbConnectionInfo(chatSession.dbConnectionInfo());
            _db->update(*result);
            INF("ChatSession updated in DB: chatSessionId={}", chatSession.chatSessionId());
        } else {
            // 不存在 → 插入新会话
            _db->persist(const_cast<ChatSessionEntity&>(chatSession));
            INF("ChatSession saved to DB: chatSessionId={}", chatSession.chatSessionId());
        }
        trans.commit();
        return true;
    } catch (const std::exception& e) {
        ERR("Failed to save ChatSession to DB: chatSessionId={}, error={}",
            chatSession.chatSessionId(), e.what());
        return false;
    }
}

// 按会话 Id 查询
std::optional<ChatSessionEntity>
ChatSessionData::getChatSessionBySessionIdFromDb(const std::string& chatSessionId) {
    try {
        odb::transaction trans(_db->begin());
        auto result = _db->query_one<ChatSessionEntity>(
            odb::query<ChatSessionEntity>::chatSessionId == chatSessionId);
        trans.commit();
        if (result) {
            return *result;
        }
        WRN("ChatSession not found in DB by chatSessionId: chatSessionId={}", chatSessionId);
        return std::nullopt;
    } catch (const std::exception& e) {
        ERR("Failed to get ChatSession from DB by chatSessionId: chatSessionId={}, error={}",
            chatSessionId, e.what());
        return std::nullopt;
    }
}

// 按用户 Id 查询全部会话（会话列表；刻意直查 MySQL：
// 缓存键是按会话粒度的，无法按用户聚合，逐会话查缓存反而更慢）
std::vector<ChatSessionEntity>
ChatSessionData::getChatSessionByUserIdFromDb(const std::string& userId) {
    std::vector<ChatSessionEntity> result;
    try {
        odb::transaction trans(_db->begin());
        odb::result<ChatSessionEntity> queryResult = _db->query<ChatSessionEntity>(
            odb::query<ChatSessionEntity>::userId == userId);
        for (const auto& session : queryResult) {
            result.push_back(session);
        }
        trans.commit();
        INF("ChatSessions found in DB by userId: userId={}, count={}", userId, result.size());
        return result;
    } catch (const std::exception& e) {
        ERR("Failed to get ChatSessions from DB by userId: userId={}, error={}",
            userId, e.what());
        return result;
    }
}

// 按会话 Id 删除
bool ChatSessionData::deleteChatSessionBySessionIdFromDb(const std::string& chatSessionId) {
    try {
        odb::transaction trans(_db->begin());
        _db->erase_query<ChatSessionEntity>(
            odb::query<ChatSessionEntity>::chatSessionId == chatSessionId);
        trans.commit();
        INF("ChatSession deleted from DB: chatSessionId={}", chatSessionId);
        return true;
    } catch (const std::exception& e) {
        ERR("Failed to delete ChatSession from DB: chatSessionId={}, error={}",
            chatSessionId, e.what());
        return false;
    }
}

// 会话元数据写入缓存（JSON 整存整取；TTL 3 天）
bool ChatSessionData::saveChatSessionToCache(const ChatSessionEntity& chatSession) {
    try {
        // 缓存键：chat_session:<chatSessionId>
        std::string key = "chat_session:" + chatSession.chatSessionId();
        Json::Value sessionJson;
        sessionJson["chatSessionId"] = chatSession.chatSessionId();
        sessionJson["userId"] = chatSession.userId();
        sessionJson["title"] = chatSession.title();
        sessionJson["createTime"] = static_cast<Json::Int64>(chatSession.createTime());
        sessionJson["updateTime"] = static_cast<Json::Int64>(chatSession.updateTime());
        sessionJson["messageCount"] = chatSession.messageCount();
        sessionJson["modelName"] = chatSession.modelName();
        sessionJson["fileId"] = chatSession.fileId();          // nullable → 空串
        sessionJson["sessionType"] = chatSession.sessionType();
        sessionJson["dbConnectionInfo"] = chatSession.dbConnectionInfo();
        auto jsonStr = biteutil::JSON::serialize(sessionJson);
        if (!jsonStr.has_value()) {
            WRN("Failed to serialize ChatSession for cache: chatSessionId={}",
                chatSession.chatSessionId());
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(_redisMutex);
            _redis->setex(key, CHAT_SESSION_CACHE_TTL_SECONDS, jsonStr.value());
        }
        INF("ChatSession saved to cache: chatSessionId={}", chatSession.chatSessionId());
        return true;
    } catch (const std::exception& e) {
        ERR("Failed to save ChatSession to cache: chatSessionId={}, error={}",
            chatSession.chatSessionId(), e.what());
        return false;
    }
}

// 从缓存读会话（未命中/损坏 → nullopt，由上层回源并回填）
std::optional<ChatSessionEntity>
ChatSessionData::getChatSessionFromCacheBySessionId(const std::string& chatSessionId) {
    try {
        std::string key = "chat_session:" + chatSessionId;
        std::string jsonStr;
        {
            std::lock_guard<std::mutex> lock(_redisMutex);
            auto result = _redis->get(key);
            if (!result.has_value() || result.value().empty()) {
                INF("ChatSession not found in cache by chatSessionId: chatSessionId={}",
                    chatSessionId);
                return std::nullopt;
            }
            jsonStr = result.value();
        }
        auto jsonOpt = biteutil::JSON::unserialize(jsonStr);
        if (!jsonOpt.has_value()) {
            WRN("Failed to unserialize ChatSession from cache: chatSessionId={}", chatSessionId);
            return std::nullopt;
        }
        ChatSessionEntity chatSession;
        chatSession.setChatSessionId(jsonOpt.value()["chatSessionId"].asString());
        chatSession.setUserId(jsonOpt.value()["userId"].asString());
        chatSession.setTitle(jsonOpt.value()["title"].asString());
        chatSession.setCreateTime(jsonOpt.value()["createTime"].asInt64());
        chatSession.setUpdateTime(jsonOpt.value()["updateTime"].asInt64());
        chatSession.setMessageCount(jsonOpt.value()["messageCount"].asInt());
        chatSession.setModelName(jsonOpt.value()["modelName"].asString());
        chatSession.setFileId(jsonOpt.value()["fileId"].asString());    // 空串 → reset → NULL
        chatSession.setSessionType(jsonOpt.value()["sessionType"].asString());
        chatSession.setDbConnectionInfo(jsonOpt.value()["dbConnectionInfo"].asString());
        return chatSession;
    } catch (const std::exception& e) {
        ERR("Failed to get ChatSession from cache by chatSessionId: chatSessionId={}, error={}",
            chatSessionId, e.what());
        return std::nullopt;
    }
}

// 从缓存删除（写 DB 成功后调用）
void ChatSessionData::deleteChatSessionFromCache(const std::string& chatSessionId) {
    try {
        std::string key = "chat_session:" + chatSessionId;
        std::lock_guard<std::mutex> lock(_redisMutex);
        auto res = _redis->del(key);
        if (res == 0) {
            INF("ChatSession not found in cache: chatSessionId={}", chatSessionId);
        } else {
            INF("ChatSession deleted from cache: chatSessionId={}", chatSessionId);
        }
    } catch (const std::exception& e) {
        ERR("Failed to delete ChatSession from cache: chatSessionId={}, error={}",
            chatSessionId, e.what());
    }
}

} // namespace aiService

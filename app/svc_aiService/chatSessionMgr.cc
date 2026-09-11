#include <bite_scaffold/log.h>
#include "chatSessionMgr.h"

namespace aiService {

ChatSessionMgr::ChatSessionMgr(std::shared_ptr<ChatSessionData> chatSessionData)
    : _chatSessionData(chatSessionData) {
    INF("ChatSessionMgr initialized");
}

// 归属校验：缓存优先（O(1)），未命中回源 DB
// 这是"会话按用户隔离"承诺的执行点——所有读/删/关联操作进数据层前先过这里
bool ChatSessionMgr::isSessionOwnedByUser(const std::string& chatSessionId,
                                          const std::string& userId) {
    auto cachedSession = _chatSessionData->getChatSessionFromCacheBySessionId(chatSessionId);
    if (cachedSession.has_value()) {
        bool owned = (cachedSession.value().userId() == userId);
        INF("Session ownership check from cache: chatSessionId={}, userId={}, owned={}",
            chatSessionId, userId, owned);
        return owned;
    }
    auto dbSession = _chatSessionData->getChatSessionBySessionIdFromDb(chatSessionId);
    if (!dbSession.has_value()) {
        WRN("Session not found: chatSessionId={}", chatSessionId);
        return false;
    }
    bool owned = (dbSession.value().userId() == userId);
    INF("Session ownership check from DB: chatSessionId={}, userId={}, owned={}",
        chatSessionId, userId, owned);
    return owned;
}

// 保存或更新：写 DB 成功后删缓存（Cache-Aside 写策略——下次读回填最新值）
bool ChatSessionMgr::saveChatSession(const ChatSessionInfo& chatSessionInfo) {
    // 1. 构建实体（fileId 空串 → reset → DB NULL；见 chatSessionEntity 的 AI4 决策）
    ChatSessionEntity entity;
    entity.setChatSessionId(chatSessionInfo._chatSessionId);
    entity.setUserId(chatSessionInfo._userId);
    entity.setTitle(chatSessionInfo._title);
    entity.setCreateTime(chatSessionInfo._createTime);
    entity.setUpdateTime(chatSessionInfo._updateTime);
    entity.setMessageCount(chatSessionInfo._messageCount);
    entity.setModelName(chatSessionInfo._modelName);
    entity.setFileId(chatSessionInfo._fileId);
    entity.setSessionType(chatSessionInfo._sessionType);
    entity.setDbConnectionInfo(chatSessionInfo._dbConnectionInfo);
    // 2. 保存或更新到 MySQL
    if (!_chatSessionData->saveChatSessionToDb(entity)) {
        ERR("Failed to save ChatSession to DB: chatSessionId={}",
            chatSessionInfo._chatSessionId);
        return false;
    }
    // 3. 删缓存（下次读回填）
    _chatSessionData->deleteChatSessionFromCache(chatSessionInfo._chatSessionId);
    INF("ChatSession saved: chatSessionId={}", chatSessionInfo._chatSessionId);
    return true;
}

// 用户会话列表：直查 MySQL（缓存按会话粒度无法按用户聚合）
std::vector<ChatSessionInfo> ChatSessionMgr::getChatSessionsByUserId(const std::string& userId) {
    std::vector<ChatSessionInfo> result;
    auto chatSessions = _chatSessionData->getChatSessionByUserIdFromDb(userId);
    for (const auto& chatSession : chatSessions) {
        ChatSessionInfo info;
        info._chatSessionId = chatSession.chatSessionId();
        info._userId = chatSession.userId();
        info._title = chatSession.title();
        info._createTime = chatSession.createTime();
        info._updateTime = chatSession.updateTime();
        info._messageCount = chatSession.messageCount();
        info._modelName = chatSession.modelName();
        info._fileId = chatSession.fileId();
        info._sessionType = chatSession.sessionType();
        info._dbConnectionInfo = chatSession.dbConnectionInfo();
        result.push_back(info);
    }
    INF("Get ChatSessions by userId: userId={}, count={}", userId, result.size());
    return result;
}

// 单个会话：缓存优先 → DB → 回填缓存（Cache-Aside 读策略）
std::optional<ChatSessionInfo> ChatSessionMgr::getChatSessionBySessionId(const std::string& sessionId) {
    auto cachedSession = _chatSessionData->getChatSessionFromCacheBySessionId(sessionId);
    if (cachedSession.has_value()) {
        const auto& chatSession = cachedSession.value();
        ChatSessionInfo info;
        info._chatSessionId = chatSession.chatSessionId();
        info._userId = chatSession.userId();
        info._title = chatSession.title();
        info._createTime = chatSession.createTime();
        info._updateTime = chatSession.updateTime();
        info._messageCount = chatSession.messageCount();
        info._modelName = chatSession.modelName();
        info._fileId = chatSession.fileId();
        info._sessionType = chatSession.sessionType();
        info._dbConnectionInfo = chatSession.dbConnectionInfo();
        INF("ChatSession found in cache: sessionId={}", sessionId);
        return info;
    }
    auto dbSession = _chatSessionData->getChatSessionBySessionIdFromDb(sessionId);
    if (!dbSession.has_value()) {
        WRN("ChatSession not found: sessionId={}", sessionId);
        return std::nullopt;
    }
    const auto& chatSession = dbSession.value();
    ChatSessionInfo info;
    info._chatSessionId = chatSession.chatSessionId();
    info._userId = chatSession.userId();
    info._title = chatSession.title();
    info._createTime = chatSession.createTime();
    info._updateTime = chatSession.updateTime();
    info._messageCount = chatSession.messageCount();
    info._modelName = chatSession.modelName();
    info._fileId = chatSession.fileId();
    info._sessionType = chatSession.sessionType();
    info._dbConnectionInfo = chatSession.dbConnectionInfo();
    _chatSessionData->saveChatSessionToCache(chatSession);   // 回填缓存
    INF("ChatSession found in DB: sessionId={}", sessionId);
    return info;
}

// 删除会话：先校验归属 → 删 DB → 删缓存
bool ChatSessionMgr::deleteChatSession(const std::string& chatSessionId, const std::string& userId) {
    // 1. 校验用户权限
    if (!isSessionOwnedByUser(chatSessionId, userId)) {
        WRN("User does not own the session: chatSessionId={}, userId={}",
            chatSessionId, userId);
        return false;
    }
    // 2. 从 MySQL 删除
    if (!_chatSessionData->deleteChatSessionBySessionIdFromDb(chatSessionId)) {
        ERR("Failed to delete ChatSession from DB: chatSessionId={}", chatSessionId);
        return false;
    }
    // 3. 删除缓存
    _chatSessionData->deleteChatSessionFromCache(chatSessionId);
    INF("ChatSession deleted: sessionId={}", chatSessionId);
    return true;
}

// 关联文件：校验归属 → 取会话 → 更新 fileId → 保存（写 DB 成功后删缓存）
bool ChatSessionMgr::associateFileId(const std::string& chatSessionId, const std::string& fileId,
                                     const std::string& userId) {
    // 1. 校验用户权限
    if (!isSessionOwnedByUser(chatSessionId, userId)) {
        WRN("User does not own the session: chatSessionId={}, userId={}",
            chatSessionId, userId);
        return false;
    }
    // 2. 获取当前会话信息（缓存优先）
    auto sessionOpt = getChatSessionBySessionId(chatSessionId);
    if (!sessionOpt.has_value()) {
        WRN("ChatSession not found: chatSessionId={}", chatSessionId);
        return false;
    }
    // 3. 更新会话的 fileId 并保存
    auto chatSession = sessionOpt.value();
    chatSession._fileId = fileId;
    if (!saveChatSession(chatSession)) {
        ERR("Failed to update ChatSession fileId: chatSessionId={}, fileId={}",
            chatSessionId, fileId);
        return false;
    }
    INF("ChatSession fileId associated: chatSessionId={}, fileId={}", chatSessionId, fileId);
    return true;
}

} // namespace aiService

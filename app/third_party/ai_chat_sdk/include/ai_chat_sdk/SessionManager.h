#pragma once
#include <atomic>
#include <unordered_map>
#include <mutex>
#include <memory>
#include "DataManager.h"
#include "common.h"


namespace ai_chat_sdk {

class SessionManager{
public:
    SessionManager(const std::string& dbName = "chatDB.db");
    // 创建会话，提供模型名称
    std::string createSession(const std::string& modelName);
    // 通过会话ID获取会话信息
    std::shared_ptr<Session>  getSession(const std::string& sessionId);
    // 往某个会话中添加消息
    bool addMessage(const std::string& sessionId, const Message& message);
    // 获取某个会话的所有历史消息
    std::vector<Message> getHistroyMessages(const std::string& sessionId)const;
    // 更新会话时间戳
    void updateSessionTimestamp(const std::string& sessionId);
    // 获取会话所有会话列表
    std::vector<std::string> getSessionLists()const;
    // 删除某个会话
    bool deleteSession(const std::string& sessionId);
    // 清空所有会话
    void clearAllSessions();
    // 获取会话总数
    size_t getSessionCount()const;

private:
    std::string generateSessionId();
    std::string generateMessageId(size_t messageCounter);

private:
    // 管理所有会话信息，key: 会话ID，value: 会话信息
    std::unordered_map<std::string, std::shared_ptr<Session>> _sessions;
    mutable std::mutex _mutex;
    std::atomic<int64_t> _sessionCounter = {0};   // 记录所有会话总数
    DataManager _dataManager;
};


} // end ai_chat_sdk

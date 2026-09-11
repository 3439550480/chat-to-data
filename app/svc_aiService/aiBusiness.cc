#include "aiBusiness.h"
#include "../common/errorHandler.h"
#include <bite_scaffold/log.h>
#include <bite_scaffold/util.h>
#include <ctime>

namespace aiService {

AIBusiness::AIBusiness(std::shared_ptr<ai_chat_sdk::ChatSDK> chatSdk,
                       std::shared_ptr<ChatSessionMgr> chatSessionMgr,
                       std::shared_ptr<biterpc::SvcChannels> svcChannels)
    : _chatSdk(chatSdk)
    , _chatSessionMgr(chatSessionMgr)
    , _svcChannels(svcChannels) {
    INF("AIBusiness initialized");
}

// 模型列表：ChatSDK 里注册了什么就返回什么（本项目只注册 GLM，即只出 GLM）
std::vector<ModelInfo> AIBusiness::getAvailableModels() {
    std::vector<ModelInfo> result;
    // 1. 从 ChatSDK 获取可用模型
    auto models = _chatSdk->getAvailableModels();
    // 2. 转换为 ModelInfo 结构
    for (const auto& model : models) {
        ModelInfo info;
        info._name = model._modelName;
        info._desc = model._modelDesc;
        result.push_back(info);
    }
    INF("Get available models: count={}", result.size());
    return result;
}

// 新建会话：ChatSDK 建会话（消息本体记账入口）→ 元数据落 MySQL（业务账本）
CreateSessionResult AIBusiness::createSession(const std::string& userId,
                                              const std::string& model,
                                              const std::string& sessionType,
                                              const std::string& dbConnectionInfo) {
    // 1. ChatSDK 创建会话（chatDB.db 落账，返回会话 Id）
    std::string chatSessionId = _chatSdk->createSession(model);
    if (chatSessionId.empty()) {
        ERR("Failed to create session in ChatSDK: userId={}, model={}", userId, model);
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::AI_SESSION_CREATE_ERROR);
    }
    // 2. 组装元数据（title 空：首条消息后回填；messageCount 0：发消息后 +2）
    int64_t now = static_cast<int64_t>(time(nullptr));   // 秒级时间戳（AI2：与 updateTime 统一）
    ChatSessionInfo chatSessionInfo;
    chatSessionInfo._chatSessionId = chatSessionId;
    chatSessionInfo._userId = userId;
    chatSessionInfo._title = "";
    chatSessionInfo._createTime = now;
    chatSessionInfo._updateTime = now;
    chatSessionInfo._messageCount = 0;
    chatSessionInfo._modelName = model;
    chatSessionInfo._fileId = "";                        // 创建时未关联文件（UpdateSessionFile 再关联）
    chatSessionInfo._sessionType = sessionType;
    chatSessionInfo._dbConnectionInfo = dbConnectionInfo;
    // 3. 元数据落 MySQL（写 DB 成功后删缓存）
    if (!_chatSessionMgr->saveChatSession(chatSessionInfo)) {
        ERR("Failed to save chat session: chatSessionId={}", chatSessionId);
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::AI_SESSION_SAVE_ERROR);
    }
    // 4. 返回结果
    CreateSessionResult result;
    result._chatSessionId = chatSessionId;
    result._model = model;
    INF("Session created: chatSessionId={}, userId={}, model={}",
        chatSessionId, userId, model);
    return result;
}

// 用户会话列表
std::vector<SessionDetailInfo> AIBusiness::getSessionList(const std::string& userId) {
    std::vector<SessionDetailInfo> result;
    // 1. 获取用户所有会话
    auto chatSessions = _chatSessionMgr->getChatSessionsByUserId(userId);
    for (const auto& chatSession : chatSessions) {
        SessionDetailInfo detail;
        detail._id = chatSession._chatSessionId;
        detail._model = chatSession._modelName;
        detail._title = chatSession._title;
        detail._createdAt = chatSession._createTime;
        detail._updatedAt = chatSession._updateTime;
        detail._messageCount = chatSession._messageCount;
        // AI8 决策：课件从不填充 _firstUserMessageContent（恒空）——
        // 以 title 兜底（plain 场景 title 即用户消息前 20 字，语义近似）
        detail._firstUserMessageContent = chatSession._title;
        detail._sessionType = chatSession._sessionType;
        detail._dbConnectionInfo = chatSession._dbConnectionInfo;
        result.push_back(detail);
    }
    // 2. 返回结果
    INF("Get session list: userId={}, count={}", userId, result.size());
    return result;
}

// 指定会话元数据（归属校验失败与不存在都返回 nullopt → 接口层报 AI_SESSION_NOT_FOUND）
std::optional<SessionDetailInfo> AIBusiness::getSession(const std::string& chatSessionId,
                                                        const std::string& userId) {
    // 1. 检查会话是否属于该用户
    if (!_chatSessionMgr->isSessionOwnedByUser(chatSessionId, userId)) {
        WRN("ChatSession not owned by user: chatSessionId={}, userId={}",
            chatSessionId, userId);
        return std::nullopt;
    }
    // 2. 获取会话信息
    auto sessionOpt = _chatSessionMgr->getChatSessionBySessionId(chatSessionId);
    if (!sessionOpt.has_value()) {
        WRN("ChatSession not found: chatSessionId={}", chatSessionId);
        return std::nullopt;
    }
    const auto& chatSession = sessionOpt.value();
    SessionDetailInfo detail;
    detail._id = chatSession._chatSessionId;
    detail._model = chatSession._modelName;
    detail._title = chatSession._title;
    detail._createdAt = chatSession._createTime;
    detail._updatedAt = chatSession._updateTime;
    detail._messageCount = chatSession._messageCount;
    detail._firstUserMessageContent = chatSession._title;   // 同 AI8
    detail._sessionType = chatSession._sessionType;
    detail._dbConnectionInfo = chatSession._dbConnectionInfo;
    INF("Get session: chatSessionId={}, userId={}", chatSessionId, userId);
    return detail;
}

// 历史消息：元数据（本表）给场景信息，消息本体从 ChatSDK 拉取
std::optional<SessionHistoryResult> AIBusiness::getSessionHistory(const std::string& chatSessionId,
                                                                  const std::string& userId) {
    // 1. 检查会话是否属于该用户
    if (!_chatSessionMgr->isSessionOwnedByUser(chatSessionId, userId)) {
        WRN("ChatSession not owned by user: chatSessionId={}, userId={}",
            chatSessionId, userId);
        return std::nullopt;
    }
    // 2. 获取会话信息
    auto sessionOpt = _chatSessionMgr->getChatSessionBySessionId(chatSessionId);
    if (!sessionOpt.has_value()) {
        WRN("ChatSession not found: chatSessionId={}", chatSessionId);
        return std::nullopt;
    }
    // 3. 消息本体从 ChatSDK 拉取（chatDB.db）
    auto chatSession = _chatSdk->getSession(chatSessionId);
    if (!chatSession) {
        WRN("ChatSession not found in ChatSDK: chatSessionId={}", chatSessionId);
        return std::nullopt;
    }
    // 4. 组装结果（关联了文件才带 file_id；前端据此跳 Excel 页面预览）
    SessionHistoryResult result;
    const auto& session = sessionOpt.value();
    result._sessionType = session._sessionType;
    result._dbConnectionInfo = session._dbConnectionInfo;
    if (!session._fileId.empty()) {
        result._fileId = session._fileId;
    }
    for (const auto& msg : chatSession->_messages) {
        HistoryMessageInfo msgInfo;
        msgInfo._messageId = msg._messageId;
        msgInfo._role = msg._role;
        msgInfo._content = msg._content;
        msgInfo._timestamp = msg._timestamp;
        result._messages.push_back(msgInfo);
    }
    INF("Get session history: sessionId={}, messageCount={}",
        chatSessionId, result._messages.size());
    return result;
}

// 删除会话：ChatSDK 会话（消息本体）+ 元数据双删
// 注意：ChatSDK 删除失败只告警不中断——元数据删掉后会话对外已不可见，
// chatDB 里的残留可后续清理（避免"元数据在但 SDK 删失败"时用户永远删不掉）
bool AIBusiness::deleteSession(const std::string& chatSessionId, const std::string& userId) {
    // 1. 删除 ChatSDK 中的会话（消息本体）
    bool deleted = _chatSdk->deleteSession(chatSessionId);
    if (!deleted) {
        WRN("Failed to delete session from ChatSDK: chatSessionId={}", chatSessionId);
    }
    // 2. 删除元数据（MySQL + 缓存；内部含归属校验）
    if (!_chatSessionMgr->deleteChatSession(chatSessionId, userId)) {
        ERR("Failed to delete chat session: chatSessionId={}", chatSessionId);
        return false;
    }
    INF("ChatSession deleted: chatSessionId={}, userId={}", chatSessionId, userId);
    return true;
}

// 更新会话关联的文件
bool AIBusiness::updateSessionFile(const std::string& chatSessionId, const std::string& userId,
                                   const std::string& fileId) {
    if (!_chatSessionMgr->associateFileId(chatSessionId, fileId, userId)) {
        ERR("Failed to update session file: chatSessionId={}, fileId={}",
            chatSessionId, fileId);
        return false;
    }
    INF("ChatSession file updated: chatSessionId={}, fileId={}", chatSessionId, fileId);
    return true;
}

// 发送消息（桩：19 章 H13 落地——归属校验 + 独立线程 + AIMessageHandler 全流程）
void AIBusiness::sendMessage(const SendMessageContext& context,
                             butil::intrusive_ptr<brpc::ProgressiveAttachment> progressiveAttachment) {
    // 等发送消息模块实现完成之后再完善
}

} // namespace aiService

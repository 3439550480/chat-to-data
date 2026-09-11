#pragma once

#include <memory>
#include <string>
#include <vector>
#include <optional>
#include "../data/chatSessionData.h"
#include "common.h"

namespace aiService {

// 聊天会话管理器：在数据层之上补"权限校验 + 业务语义"
// （ChatSDK 不区分用户 → 归属校验是本类的核心价值）
class ChatSessionMgr {
public:
    ChatSessionMgr(std::shared_ptr<ChatSessionData> chatSessionData);

    // 检测会话是否属于指定用户（缓存优先，未命中回源 DB）
    bool isSessionOwnedByUser(const std::string& chatSessionId, const std::string& userId);
    // 保存或更新会话（写 DB 成功后删缓存——Cache-Aside 写策略）
    bool saveChatSession(const ChatSessionInfo& chatSessionInfo);
    // 用户会话列表（直查 MySQL，不走缓存）
    std::vector<ChatSessionInfo> getChatSessionsByUserId(const std::string& userId);
    // 单个会话元数据（缓存优先 → DB → 回填缓存）
    std::optional<ChatSessionInfo> getChatSessionBySessionId(const std::string& chatSessionId);
    // 删除会话（需归属校验；DB 删除成功后删缓存）
    bool deleteChatSession(const std::string& chatSessionId, const std::string& userId);
    // 将文件 Id 关联到会话（需归属校验；更新 fileId 后删缓存）
    bool associateFileId(const std::string& chatSessionId, const std::string& fileId,
                         const std::string& userId);

private:
    std::shared_ptr<ChatSessionData> _chatSessionData;  // 数据层实例指针
};

} // namespace aiService

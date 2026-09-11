#pragma once

#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <thread>
#include <brpc/controller.h>              // ProgressiveAttachment（流式响应）
#include <bite_scaffold/rpc.h>
#include <ai_chat_sdk/ChatSDK.h>
#include "chatSessionMgr.h"
#include "common.h"
#include "../proto/protoCode/aiService.pb.h"

namespace aiService {

// AI 业务层：缝合 ChatSDK（模型交互 + 消息落库）与 ChatSessionMgr（用户隔离的会话账本）
class AIBusiness {
public:
    AIBusiness(std::shared_ptr<ai_chat_sdk::ChatSDK> chatSdk,
               std::shared_ptr<ChatSessionMgr> chatSessionMgr,
               std::shared_ptr<biterpc::SvcChannels> svcChannels);

    // 获取可用的模型列表（ChatSDK 内注册了什么就返回什么——本项目只注册 GLM）
    std::vector<ModelInfo> getAvailableModels();
    // 新建聊天会话（ChatSDK 建会话 + 元数据落 MySQL）
    // @throws AI_SESSION_CREATE_ERROR / AI_SESSION_SAVE_ERROR
    CreateSessionResult createSession(const std::string& userId,
                                      const std::string& model,
                                      const std::string& sessionType,
                                      const std::string& dbConnectionInfo);
    // 用户会话列表
    std::vector<SessionDetailInfo> getSessionList(const std::string& userId);
    // 指定会话元数据（需归属校验）
    // @return nullopt = 不存在或不属于该用户
    std::optional<SessionDetailInfo> getSession(const std::string& chatSessionId,
                                                const std::string& userId);
    // 指定会话历史消息（需归属校验；消息本体从 ChatSDK 拉取）
    std::optional<SessionHistoryResult> getSessionHistory(const std::string& chatSessionId,
                                                          const std::string& userId);
    // 删除会话（ChatSDK 会话 + 元数据双删；需归属校验）
    bool deleteSession(const std::string& chatSessionId, const std::string& userId);
    // 更新会话关联的文件（文件子服务 associateFileChatSession 经 RPC 到此）
    bool updateSessionFile(const std::string& chatSessionId, const std::string& userId,
                           const std::string& fileId);
    // 发送消息（19 章 H13 落地：归属校验 + 独立线程 + SSE 流式全流程）
    // context 发送消息上下文；progressiveAttachment 流式响应器（SSE 传输）
    void sendMessage(const SendMessageContext& context,
                     butil::intrusive_ptr<brpc::ProgressiveAttachment> progressiveAttachment);

private:
    std::shared_ptr<ai_chat_sdk::ChatSDK> _chatSdk;       // ChatSDK 实例
    std::shared_ptr<ChatSessionMgr> _chatSessionMgr;      // 会话管理器
    std::shared_ptr<biterpc::SvcChannels> _svcChannels;   // RPC 通信（H10 起使用）
};

} // namespace aiService

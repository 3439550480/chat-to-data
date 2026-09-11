#include <exception>
#include <butil/logging.h>
#include <brpc/controller.h>
#include <brpc/server.h>
#include <bite_scaffold/log.h>
#include <bite_scaffold/rpc.h>
#include "../common/errorHandler.h"
#include "aiServiceImpl.h"
#include "aiBusiness.h"

namespace aiService {

AIServiceImpl::AIServiceImpl(std::shared_ptr<AIBusiness> aiBusiness)
    : _aiBusiness(aiBusiness) {
}

AIServiceImpl::~AIServiceImpl() {}

// 获取支持的模型列表
void AIServiceImpl::GetModels(::google::protobuf::RpcController* controller,
                              const chat2Data::AiService::GetModelsRequest* request,
                              chat2Data::AiService::GetModelsResponse* response,
                              ::google::protobuf::Closure* done) {
    // 1. 构造 ClosureGuard 对象（RAII 管理 done->Run()）
    brpc::ClosureGuard done_guard(done);
    // 2. 解析请求参数
    std::string requestId = request->request_id();
    // 3. 调用业务层获取模型列表
    try {
        auto models = _aiBusiness->getAvailableModels();
        // 4. 构造 RPC 响应
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
        for (const auto& model : models) {
            auto* modelInfo = response->mutable_result()->add_models();
            modelInfo->set_name(model._name);
            modelInfo->set_desc(model._desc);
        }
        INF("GetModels success: requestId={}, modelCount={}", requestId, models.size());
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

// 创建新会话
void AIServiceImpl::CreateSession(::google::protobuf::RpcController* controller,
                                  const chat2Data::AiService::CreateChatSessionRequest* request,
                                  chat2Data::AiService::CreateChatSessionResponse* response,
                                  ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string userId = request->user_id();
    std::string model = request->model();
    std::string sessionType = request->session_type();
    std::string dbConnectionInfo = request->db_connection_info();
    // 校验参数
    if (userId.empty() || model.empty() || sessionType.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::AI_PARAM_INVALID));
        response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::AI_PARAM_INVALID));
        return;
    }
    try {
        auto result = _aiBusiness->createSession(userId, model, sessionType, dbConnectionInfo);
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
        response->mutable_result()->mutable_session()->set_chat_session_id(result._chatSessionId);
        response->mutable_result()->mutable_session()->set_model(result._model);
        INF("CreateSession success: requestId={}, chatSessionId={}", requestId, result._chatSessionId);
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

// 获取会话列表
void AIServiceImpl::GetSessions(::google::protobuf::RpcController* controller,
                                const chat2Data::AiService::GetSessionsRequest* request,
                                chat2Data::AiService::GetSessionsResponse* response,
                                ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string userId = request->user_id();
    if (userId.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::AI_PARAM_INVALID));
        response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::AI_PARAM_INVALID));
        return;
    }
    try {
        auto sessions = _aiBusiness->getSessionList(userId);
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
        for (const auto& session : sessions) {
            auto* sessionInfo = response->mutable_result()->add_sessioninfo();
            sessionInfo->set_id(session._id);
            sessionInfo->set_model(session._model);
            sessionInfo->set_title(session._title);
            sessionInfo->set_created_at(session._createdAt);
            sessionInfo->set_updated_at(session._updatedAt);
            sessionInfo->set_message_count(session._messageCount);
            sessionInfo->set_first_user_message_content(session._firstUserMessageContent);
            sessionInfo->set_session_type(session._sessionType);
            sessionInfo->set_db_connection_info(session._dbConnectionInfo);
        }
        INF("GetSessions success: requestId={}, sessionCount={}", requestId, sessions.size());
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

// 获取指定会话的历史消息
void AIServiceImpl::GetSessionHistory(::google::protobuf::RpcController* controller,
                                      const chat2Data::AiService::GetSessionHistoryRequest* request,
                                      chat2Data::AiService::GetSessionHistoryResponse* response,
                                      ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string userId = request->user_id();
    std::string chatSessionId = request->chat_session_id();
    if (userId.empty() || chatSessionId.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::AI_PARAM_INVALID));
        response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::AI_PARAM_INVALID));
        return;
    }
    try {
        auto result = _aiBusiness->getSessionHistory(chatSessionId, userId);
        if (!result.has_value()) {
            response->set_request_id(requestId);
            response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::AI_SESSION_NOT_FOUND));
            response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::AI_SESSION_NOT_FOUND));
            return;
        }
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
        auto& historyResult = result.value();
        if (!historyResult._fileId.empty()) {
            response->mutable_result()->set_file_id(historyResult._fileId);
        }
        response->mutable_result()->set_session_type(historyResult._sessionType);
        response->mutable_result()->set_db_connection_info(historyResult._dbConnectionInfo);
        for (const auto& msg : historyResult._messages) {
            auto* msgProto = response->mutable_result()->add_messages();
            msgProto->set_id(msg._messageId);
            msgProto->set_role(msg._role);
            msgProto->set_content(msg._content);
            msgProto->set_timestamp(msg._timestamp);
        }
        INF("GetSessionHistory success: requestId={}, chatSessionId={}, messageCount={}",
            requestId, chatSessionId, historyResult._messages.size());
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

// 删除会话
void AIServiceImpl::DeleteSession(::google::protobuf::RpcController* controller,
                                  const chat2Data::AiService::DeleteSessionRequest* request,
                                  chat2Data::AiService::DeleteSessionResponse* response,
                                  ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string userId = request->user_id();
    std::string chatSessionId = request->chat_session_id();
    if (userId.empty() || chatSessionId.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::AI_PARAM_INVALID));
        response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::AI_PARAM_INVALID));
        return;
    }
    try {
        bool success = _aiBusiness->deleteSession(chatSessionId, userId);
        if (!success) {
            response->set_request_id(requestId);
            response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::AI_SESSION_NOT_FOUND));
            response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::AI_SESSION_NOT_FOUND));
            return;
        }
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
        INF("DeleteSession success: requestId={}, chatSessionId={}", requestId, chatSessionId);
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

// 更新会话文件关联
void AIServiceImpl::UpdateSessionFile(::google::protobuf::RpcController* controller,
                                      const chat2Data::AiService::UpdateSessionFileRequest* request,
                                      chat2Data::AiService::UpdateSessionFileResponse* response,
                                      ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string userId = request->user_id();
    std::string chatSessionId = request->chat_session_id();
    std::string fileId = request->file_id();
    if (userId.empty() || chatSessionId.empty() || fileId.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::AI_PARAM_INVALID));
        response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::AI_PARAM_INVALID));
        return;
    }
    try {
        bool success = _aiBusiness->updateSessionFile(chatSessionId, userId, fileId);
        if (!success) {
            response->set_request_id(requestId);
            response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::AI_SESSION_NOT_FOUND));
            response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::AI_SESSION_NOT_FOUND));
            return;
        }
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
        INF("UpdateSessionFile success: requestId={}, chatSessionId={}, fileId={}",
            requestId, chatSessionId, fileId);
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

// 发送消息（服务端流式响应）
// ★ 与普通 handler 的三大差异（都是流式的代价，注释写明）：
//   ① 不用 ClosureGuard：连接不能随 RPC 线程释放，否则流式传输中断
//   ② CreateProgressiveAttachment 后立刻 done->Run()：RPC 线程归还线程池，连接保留
//   ③ 推流在 AIBusiness 的独立线程进行，本函数不关心结果
void AIServiceImpl::SendMessage(::google::protobuf::RpcController* controller,
                                const chat2Data::AiService::SendMessageRequest* request,
                                chat2Data::AiService::SendMessageResponse* response,
                                ::google::protobuf::Closure* done) {
    brpc::Controller* cntl = static_cast<brpc::Controller*>(controller);
    // 1. 解析请求参数
    std::string requestId = request->request_id();
    std::string userId = request->user_id();
    std::string sessionId = request->session_id();
    std::string chatSessionId = request->chat_session_id();
    std::string chatType = request->chat_type();
    std::string message = request->message();
    std::string fileId = request->file_id();
    auto dbType = request->db_type();
    std::string dbConnectId = request->db_connect_id();
    std::string tableName = request->table_name();
    INF("SendMessage called: requestId={}, userId={}, chatSessionId={}, chatType={}",
        requestId, userId, chatSessionId, chatType);
    // 2. 校验参数（不合法：手动 done->Run() 后返回）
    if (chatSessionId.empty() || message.empty() || sessionId.empty()) {
        WRN("SendMessage params invalid: chatSessionId or message or sessionId is empty");
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::AI_PARAM_INVALID));
        response->set_error_msg("参数不完整：chatSessionId、message和sessionId不能为空");
        done->Run();
        return;
    }
    // 3. 设置 SSE 响应头（前端 EventSource 依赖这些头）
    cntl->http_response().set_content_type("text/event-stream");
    cntl->http_response().SetHeader("Cache-Control", "no-cache");
    cntl->http_response().SetHeader("Connection", "keep-alive");
    cntl->http_response().SetHeader("Access-Control-Allow-Origin", "*");
    cntl->http_response().SetHeader("Access-Control-Allow-Headers", "*");
    response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
    response->mutable_result()->set_done(false);
    // 4. 创建流式响应器后立即 done->Run()：
    //    RPC 线程归还线程池，HTTP 连接由 progressiveAttachment 持有；
    //    后续推流在 AIBusiness 的独立线程进行
    auto progressiveAttachment = cntl->CreateProgressiveAttachment();
    done->Run();
    try {
        // 5. 构建发送消息上下文
        SendMessageContext context;
        context._requestId = requestId;
        context._sessionId = sessionId;
        context._chatSessionId = chatSessionId;
        context._userId = userId;
        context._message = message;
        context._chatType = chatType;
        context._fileId = fileId;
        context._dbType = dbType;
        context._dbConnectId = dbConnectId;
        // 数据库场景：多个表名以逗号分隔，需拆分（H10 的 getDatabaseTables 处理）
        context._tableNames = {tableName};
        // 6. 交给业务层（内部起独立线程推流；此处不再调 done->Run()）
        _aiBusiness->sendMessage(context, progressiveAttachment);
    } catch (const chat2Data::Chat2DataException& e) {
        // 异常路径也走流（响应头已发出，不能改 error_code 了）
        std::string errorMsg = "data: " + chat2Data::error2String(e.getErrorCode()) + "\n\n";
        progressiveAttachment->Write(errorMsg.c_str(), errorMsg.size());
        errorMsg = "data: DONE\n\n";
        progressiveAttachment->Write(errorMsg.c_str(), errorMsg.size());
    }
}

} // namespace aiService

#pragma once
#include <string>
#include <memory>
#include <bite_scaffold/rpc.h>
#include "../proto/protoCode/aiService.pb.h"

namespace aiService {

class AIBusiness;

// AI 服务 RPC 接口实现：继承 proto 生成的 AIService 虚基类
// SendMessage 为服务端流式（不用 ClosureGuard，手动控制连接释放）
class AIServiceImpl : public chat2Data::AiService::AIService {
public:
    explicit AIServiceImpl(std::shared_ptr<AIBusiness> aiBusiness);
    ~AIServiceImpl() override;

    // 获取支持模型
    void GetModels(::google::protobuf::RpcController* controller,
                   const chat2Data::AiService::GetModelsRequest* request,
                   chat2Data::AiService::GetModelsResponse* response,
                   ::google::protobuf::Closure* done) override;
    // 创建新会话
    void CreateSession(::google::protobuf::RpcController* controller,
                       const chat2Data::AiService::CreateChatSessionRequest* request,
                       chat2Data::AiService::CreateChatSessionResponse* response,
                       ::google::protobuf::Closure* done) override;
    // 获取会话列表
    void GetSessions(::google::protobuf::RpcController* controller,
                     const chat2Data::AiService::GetSessionsRequest* request,
                     chat2Data::AiService::GetSessionsResponse* response,
                     ::google::protobuf::Closure* done) override;
    // 获取指定会话的历史消息
    void GetSessionHistory(::google::protobuf::RpcController* controller,
                           const chat2Data::AiService::GetSessionHistoryRequest* request,
                           chat2Data::AiService::GetSessionHistoryResponse* response,
                           ::google::protobuf::Closure* done) override;
    // 删除会话
    void DeleteSession(::google::protobuf::RpcController* controller,
                       const chat2Data::AiService::DeleteSessionRequest* request,
                       chat2Data::AiService::DeleteSessionResponse* response,
                       ::google::protobuf::Closure* done) override;
    // 更新会话文件关联
    void UpdateSessionFile(::google::protobuf::RpcController* controller,
                           const chat2Data::AiService::UpdateSessionFileRequest* request,
                           chat2Data::AiService::UpdateSessionFileResponse* response,
                           ::google::protobuf::Closure* done) override;
    // 发送消息（服务端流式响应）
    void SendMessage(::google::protobuf::RpcController* controller,
                     const chat2Data::AiService::SendMessageRequest* request,
                     chat2Data::AiService::SendMessageResponse* response,
                     ::google::protobuf::Closure* done) override;

private:
    std::shared_ptr<AIBusiness> _aiBusiness;   // 业务层实例指针
};

} // namespace aiService

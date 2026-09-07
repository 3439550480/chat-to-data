#pragma once
#include <string>
#include <memory>
#include <google/protobuf/service.h>
#include "../proto/protoCode/notifyService.pb.h"
#include "notifyBusiness.h"

namespace notifyService {

// 通知服务 RPC 接口实现：继承 proto 生成的 NotifyService 虚基类
class NotifyServiceImpl : public chat2Data::notifyService::NotifyService {
public:
    explicit NotifyServiceImpl(std::shared_ptr<NotifyBusiness> notifyBusiness);
    ~NotifyServiceImpl() override;
    // 发送验证码
    void SendVerifyCode(google::protobuf::RpcController* controller,
                        const chat2Data::notifyService::SendVerifyCodeRequest* request,
                        chat2Data::notifyService::SendVerifyCodeResponse* response,
                        google::protobuf::Closure* done) override;
    // 发送普通邮件
    void SendEmail(google::protobuf::RpcController* controller,
                   const chat2Data::notifyService::SendEmailRequest* request,
                   chat2Data::notifyService::SendEmailResponse* response,
                   google::protobuf::Closure* done) override;
private:
    std::shared_ptr<NotifyBusiness> _notifyBusiness;   // 业务层指针
};

} // namespace notifyService

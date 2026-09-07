#include "notifyServiceImpl.h"
#include <brpc/controller.h>
#include <bite_scaffold/log.h>
#include "../common/errorHandler.h"

namespace notifyService {

NotifyServiceImpl::NotifyServiceImpl(std::shared_ptr<NotifyBusiness> notifyBusiness)
    : _notifyBusiness(notifyBusiness) {
}

NotifyServiceImpl::~NotifyServiceImpl() {}

void NotifyServiceImpl::SendVerifyCode(google::protobuf::RpcController* controller,
                        const chat2Data::notifyService::SendVerifyCodeRequest* request,
                        chat2Data::notifyService::SendVerifyCodeResponse* response,
                        google::protobuf::Closure* done) {
    // 1. ClosureGuard 以 RAII 机制管理 done->Run()（无论 return 还是抛异常都会应答）
    brpc::ClosureGuard done_guard(done);
    // 2. 解析 rpc 请求参数
    std::string requestId = request->request_id();
    std::string email = request->email();
    std::string code = request->code();
    try {
        // 3. 校验参数
        if (email.empty()) {
            throw chat2Data::Chat2DataException(chat2Data::ErrorCode::NOTIFY_EMAIL_INVALID);
        }
        if (code.empty()) {
            throw chat2Data::Chat2DataException(chat2Data::ErrorCode::NOTIFY_CODE_INVALID);
        }
        // 4. 调用业务层（异步：入队即返回，发送在 EmailWorker 线程完成）
        _notifyBusiness->sendVerifyCodeEmail(email, code);
        // 5. 设置 rpc 响应参数
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
        INF("SendVerifyCode success: requestId={}, email={}", requestId, email);
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
        ERR("SendVerifyCode failed: {}", e.what());
    }
}

void NotifyServiceImpl::SendEmail(google::protobuf::RpcController* controller,
                   const chat2Data::notifyService::SendEmailRequest* request,
                   chat2Data::notifyService::SendEmailResponse* response,
                   google::protobuf::Closure* done) {
    // 1. ClosureGuard 以 RAII 机制管理 done->Run()
    brpc::ClosureGuard done_guard(done);
    // 2. 解析 rpc 请求参数
    std::string requestId = request->request_id();
    std::string toEmail = request->to_email();
    std::string subject = request->subject();
    std::string content = request->content();
    try {
        // 3. 校验参数
        if (toEmail.empty()) {
            throw chat2Data::Chat2DataException(chat2Data::ErrorCode::NOTIFY_EMAIL_INVALID);
        }
        if (subject.empty()) {
            throw chat2Data::Chat2DataException(chat2Data::ErrorCode::NOTIFY_SUBJECT_INVALID);
        }
        // 4. 调用业务层发送普通邮件
        _notifyBusiness->sendNormalEmail(toEmail, subject, content);
        // 5. 设置 rpc 响应参数
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
        INF("SendEmail success: requestId={}, toEmail={}", requestId, toEmail);
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
        ERR("SendEmail failed: {}", e.what());
    }
}

} // namespace notifyService

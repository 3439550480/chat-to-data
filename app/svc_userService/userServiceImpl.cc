#include <exception>
#include <bite_scaffold/log.h>
#include <bite_scaffold/rpc.h>
#include "userServiceImpl.h"
#include "userBusiness.h"
#include "../common/errorHandler.h"

namespace userService {

// 统一五步模板（10 个 handler 共用）：
// 0. brpc::ClosureGuard done_guard(done) —— RAII 管理 done->Run()（忘调 = 请求挂死）
// 1. 解析RPC请求中参数
// 2. 参数校验（空参直接填错误码返回）
// 3. try 调业务层
// 4. 成功填响应（SUCCESS；带 result 的接口用 mutable_result()->set_xxx）
// 5. catch(Chat2DataException) → e.getErrorCode()/error2String 转响应字段
//    （错误码穿透：业务异常 → RPC 响应字段，网关由此拿到结构化错误）

UserServiceImpl::UserServiceImpl(std::shared_ptr<UserBusiness> userBusiness)
    : _userBusiness(userBusiness) {
}

UserServiceImpl::~UserServiceImpl() {
}

void UserServiceImpl::ValidNickname(google::protobuf::RpcController* controller,
                            const chat2Data::userService::ValidNicknameRequest* request,
                            chat2Data::userService::ValidNicknameResponse* response,
                            google::protobuf::Closure* done) {
    // 0. 构造ClosureGuard对象，该对象以RAII机制管理done->Run()方法的调用
    brpc::ClosureGuard done_guard(done);
    // 1. 解析RPC请求中参数
    std::string requestId = request->request_id();
    std::string nickname = request->nickname();
    // 2. 参数校验
    if (nickname.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::USER_NICKNAME_EMPTY));
        response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::USER_NICKNAME_EMPTY));
        return;
    }

    try {
        // 3. 业务逻辑处理：调用业务逻辑层检测昵称是否唯一
        bool isUnique = _userBusiness->isNicknameUnique(nickname);
        // 4. 设置RPC响应
        response->set_request_id(requestId);
        if (isUnique) {
            response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
        } else {
            response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::USER_NICKNAME_EXIST));
            response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::USER_NICKNAME_EXIST));
        }
    } catch (const chat2Data::Chat2DataException& e) {
        // 5. 业务层抛出异常，统一按业务处理失败逻辑处理
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

void UserServiceImpl::ValidEmail(google::protobuf::RpcController* controller,
                                 const chat2Data::userService::ValidEmailRequest* request,
                                 chat2Data::userService::ValidEmailResponse* response,
                                 google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string email = request->email();
    if (email.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::USER_EMAIL_EMPTY));
        response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::USER_EMAIL_EMPTY));
        return;
    }

    try {
        bool isUnique = _userBusiness->isEmailUnique(email);
        response->set_request_id(requestId);
        if (isUnique) {
            response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
        } else {
            response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::USER_EMAIL_EXIST));
            response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::USER_EMAIL_EXIST));
        }
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

void UserServiceImpl::UserRegister(google::protobuf::RpcController* controller,
                               const chat2Data::userService::UserRegisterRequest* request,
                               chat2Data::userService::UserRegisterResponse* response,
                               google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string nickname = request->nickname();
    std::string password = request->password();
    std::string email = request->email();
    if (nickname.empty() || password.empty() || email.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::USER_REGISTER_PARAM_ERROR));
        response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::USER_REGISTER_PARAM_ERROR));
        return;
    }

    try {
        _userBusiness->registerUser(nickname, email, password);
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

void UserServiceImpl::SessionLogin(google::protobuf::RpcController* controller,
                                const chat2Data::userService::SessionLoginRequest* request,
                                chat2Data::userService::SessionLoginResponse* response,
                                google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string sessionId = request->session_id();
    if (sessionId.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::USER_SESSION_INVALID));
        response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::USER_SESSION_INVALID));
        return;
    }

    try {
        bool loginResult = _userBusiness->loginWithSession(sessionId);
        response->set_request_id(requestId);
        if (loginResult) {
            response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
        } else {
            response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::USER_SESSION_LOGIN_FAILED));
            response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::USER_SESSION_LOGIN_FAILED));
        }
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

void UserServiceImpl::PasswdLogin(google::protobuf::RpcController* controller,
                               const chat2Data::userService::PasswdLoginRequest* request,
                               chat2Data::userService::PasswdLoginResponse* response,
                               google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string username = request->username();
    std::string password = request->password();
    if (username.empty() || password.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::USER_PASSWORD_INVALID));
        response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::USER_PASSWORD_INVALID));
        return;
    }

    try {
        std::string sessionId = _userBusiness->loginWithPassword(username, password);
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
        response->mutable_result()->set_session_id(sessionId);
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

void UserServiceImpl::GetCode(google::protobuf::RpcController* controller,
                              const chat2Data::userService::GetCodeRequest* request,
                              chat2Data::userService::GetCodeResponse* response,
                              google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string email = request->email();
    if (email.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::USER_EMAIL_EMPTY));
        response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::USER_EMAIL_EMPTY));
        return;
    }

    try {
        std::string codeId = _userBusiness->getVerifyCode(email);
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
        response->mutable_result()->set_code_id(codeId);
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

void UserServiceImpl::VcodeLogin(google::protobuf::RpcController* controller,
                                 const chat2Data::userService::VcodeLoginRequest* request,
                                 chat2Data::userService::VcodeLoginResponse* response,
                                 google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string email = request->email();
    std::string verifyCode = request->verify_code();
    std::string codeId = request->code_id();
    if (email.empty() || verifyCode.empty() || codeId.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::USER_PASSWORD_INVALID));
        response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::USER_PASSWORD_INVALID));
        return;
    }

    try {
        std::string sessionId = _userBusiness->loginWithVerifyCode(email, codeId, verifyCode);
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
        response->mutable_result()->set_session_id(sessionId);
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

void UserServiceImpl::Logout(google::protobuf::RpcController* controller,
                            const chat2Data::userService::LogoutRequest* request,
                            chat2Data::userService::LogoutResponse* response,
                            google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string sessionId = request->session_id();
    if (sessionId.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::USER_SESSION_INVALID));
        response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::USER_SESSION_INVALID));
        return;
    }

    try {
        bool logoutResult = _userBusiness->logout(sessionId);
        response->set_request_id(requestId);
        if (logoutResult) {
            response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
        } else {
            response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::USER_LOGOUT_FAILED));
            response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::USER_LOGOUT_FAILED));
        }
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

void UserServiceImpl::GetUserInfo(google::protobuf::RpcController* controller,
                                 const chat2Data::userService::GetUserInfoRequest* request,
                                 chat2Data::userService::GetUserInfoResponse* response,
                                 google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string sessionId = request->session_id();
    if (sessionId.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::USER_GET_USER_INFO_PARAM_ERROR));
        response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::USER_GET_USER_INFO_PARAM_ERROR));
        return;
    }

    try {
        UserInfo userInfo = _userBusiness->getUserInfo(sessionId);
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
        // ⚠️ 密码脱敏边界：只回传 user_id/nickname/email，userInfo._password 刻意不透出
        chat2Data::userService::UserInfo* resultUserInfo = response->mutable_result()->mutable_user_info();
        resultUserInfo->set_user_id(userInfo._userId);
        resultUserInfo->set_nickname(userInfo._nickname);
        resultUserInfo->set_email(userInfo._email);
        INF("Get user info: userId={}, nickname={}, email={}", userInfo._userId, userInfo._nickname, userInfo._email);
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

void UserServiceImpl::IsSessionValid(google::protobuf::RpcController* controller,
                              const chat2Data::userService::IsSessionValidRequest* request,
                              chat2Data::userService::IsSessionValidResponse* response,
                              google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string sessionId = request->session_id();
    if (sessionId.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::USER_SESSION_INVALID));
        response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::USER_SESSION_INVALID));
        response->set_is_valid(false);
        return;
    }

    try {
        std::string userId;
        bool isValid = _userBusiness->isSessionValid(sessionId, userId);
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
        response->set_is_valid(isValid);
        response->set_user_id(userId);
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

} // namespace userService

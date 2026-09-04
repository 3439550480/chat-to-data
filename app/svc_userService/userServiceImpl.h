#pragma once
#include <string>
#include <bite_scaffold/rpc.h>
#include "../proto/protoCode/userService.pb.h"

namespace userService {

class UserBusiness;

// RPC 服务实现类：把 brpc 泛化服务的请求/响应翻译成业务层调用
// 职责边界：只做"解析参数 → 校验 → 调业务 → 填响应"，不含任何业务逻辑
class UserServiceImpl : public chat2Data::userService::UserService {
public:
    explicit UserServiceImpl(std::shared_ptr<UserBusiness> userBusiness);
    ~UserServiceImpl() override;

    // 验证昵称是否存在
    void ValidNickname(google::protobuf::RpcController* controller,
                       const chat2Data::userService::ValidNicknameRequest* request,
                       chat2Data::userService::ValidNicknameResponse* response,
                       google::protobuf::Closure* done) override;
    // 验证邮箱是否存在
    void ValidEmail(google::protobuf::RpcController* controller,
                    const chat2Data::userService::ValidEmailRequest* request,
                    chat2Data::userService::ValidEmailResponse* response,
                    google::protobuf::Closure* done) override;
    // 用户注册
    void UserRegister(google::protobuf::RpcController* controller,
                      const chat2Data::userService::UserRegisterRequest* request,
                      chat2Data::userService::UserRegisterResponse* response,
                      google::protobuf::Closure* done) override;
    // 会话登录
    void SessionLogin(google::protobuf::RpcController* controller,
                      const chat2Data::userService::SessionLoginRequest* request,
                      chat2Data::userService::SessionLoginResponse* response,
                      google::protobuf::Closure* done) override;
    // 密码登录（username 可能是用户昵称，也可能是用户邮箱）
    void PasswdLogin(google::protobuf::RpcController* controller,
                     const chat2Data::userService::PasswdLoginRequest* request,
                     chat2Data::userService::PasswdLoginResponse* response,
                     google::protobuf::Closure* done) override;
    // 获取验证码
    void GetCode(google::protobuf::RpcController* controller,
                 const chat2Data::userService::GetCodeRequest* request,
                 chat2Data::userService::GetCodeResponse* response,
                 google::protobuf::Closure* done) override;
    // 验证码登录
    void VcodeLogin(google::protobuf::RpcController* controller,
                    const chat2Data::userService::VcodeLoginRequest* request,
                    chat2Data::userService::VcodeLoginResponse* response,
                    google::protobuf::Closure* done) override;
    // 退出登录
    void Logout(google::protobuf::RpcController* controller,
                const chat2Data::userService::LogoutRequest* request,
                chat2Data::userService::LogoutResponse* response,
                google::protobuf::Closure* done) override;
    // 获取用户信息
    void GetUserInfo(google::protobuf::RpcController* controller,
                     const chat2Data::userService::GetUserInfoRequest* request,
                     chat2Data::userService::GetUserInfoResponse* response,
                     google::protobuf::Closure* done) override;
    // 检查会话是否有效
    void IsSessionValid(google::protobuf::RpcController* controller,
                        const chat2Data::userService::IsSessionValidRequest* request,
                        chat2Data::userService::IsSessionValidResponse* response,
                        google::protobuf::Closure* done) override;
private:
    std::shared_ptr<UserBusiness> _userBusiness;   // 完整业务逻辑层实现
};

} // namespace userService

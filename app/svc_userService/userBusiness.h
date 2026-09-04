#pragma once

#include <string>
#include <memory>
#include <ctime>
#include <gflags/gflags.h>
#include <bite_scaffold/rpc.h>
#include "../data/userData.h"
#include "../data/verifyCodeData.h"
#include "sessionManager.h"

// 声明gflags变量（在main.cc中定义）——服务发现：验证码邮件找 notify、登出删连接找 db
DECLARE_string(notify_service);
DECLARE_string(db_service);

namespace userService {

// 用户业务操作类：用户 CRUD（UserData）+ 会话生命周期（SessionManager）
// + 验证码（VerifyCodeData）+ 跨服务协作（notify 发邮件 / db 删连接）的业务编排
class UserBusiness {
public:
    UserBusiness(std::shared_ptr<SessionManager> sessionManager,
                 std::shared_ptr<VerifyCodeData> verifyCodeData,
                 std::shared_ptr<UserData> userData,
                 biterpc::SvcChannels::ptr svcChannels);

    // 检查昵称是否唯一
    bool isNicknameUnique(const std::string& nickname);
    // 检查邮箱是否唯一
    bool isEmailUnique(const std::string& email);
    // 注册用户（密码在此处加密）
    // @return 新用户的 userId
    // @throws USER_SAVE_USER_INFO_FAILED
    std::string registerUser(const std::string& nickname,
                             const std::string& email,
                             const std::string& password);
    // 昵称(邮箱)+密码登录
    // @return sessionId
    // @throws USER_NOT_FOUND / USER_LOGIN_FAILED
    std::string loginWithPassword(const std::string& username,
                                  const std::string& password);
    // 生成验证码（桩：等通知子服务完成后完善——落库+调 notify 发邮件）
    std::string getVerifyCode(const std::string& email);
    // 验证码登录
    // @throws USER_VERIFY_CODE_EXPIRED / USER_VERIFY_CODE_ERROR / USER_NOT_FOUND
    std::string loginWithVerifyCode(const std::string& email,
                                    const std::string& codeId,
                                    const std::string& verifyCode);
    // 会话登录（续期）
    // @throws USER_NOT_FOUND
    bool loginWithSession(const std::string& sessionId);
    // 退出登录（状态离线 + 删会话 + 通知 db 删连接[桩]）
    bool logout(const std::string& sessionId);
    // 获取用户信息
    // @throws USER_SESSION_INVALID / USER_NOT_FOUND
    UserInfo getUserInfo(const std::string& sessionId);
    // 检查会话是否有效（网关鉴权专用）
    // @return 用户状态是否为 Online；userId 通过出参带回
    bool isSessionValid(const std::string& sessionId, std::string& userId);

private:
    // Cache-Aside 读路径封装：缓存优先，未命中回源 DB 并回填
    std::optional<UserInfo> getUserByUserId(const std::string& userId);
    std::optional<UserInfo> getUserByNickname(const std::string& nickname);
    std::optional<UserInfo> getUserByEmail(const std::string& email);
    // 密码加密（bcrypt，随机盐）
    std::string encryptPassword(const std::string& password);
    // 验证密码（用 DB 哈希串做盐重新加密比对）
    bool verifyPassword(const std::string& password, const std::string& encrypted);
    // 删除用户创建的所有数据库连接（桩：等数据库子服务完成后完善）
    void deleteUserAllConns(const std::string& userId);

private:
    std::shared_ptr<SessionManager> _sessionManager;      // 会话管理器
    std::shared_ptr<VerifyCodeData> _verifyCodeData;      // 验证码数据
    std::shared_ptr<UserData> _userData;                  // 用户数据
    biterpc::SvcChannels::ptr _svcChannels;               // 服务通道（跨服务 RPC）
};

} // namespace userService

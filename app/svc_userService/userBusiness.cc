#include <random>
#include <crypt.h>
#include <brpc/controller.h>
#include <bite_scaffold/log.h>
#include <bite_scaffold/util.h>
#include "userBusiness.h"
// 课件问题⑪回收：notifyService proto 已建（13章），include 恢复；
// dbService 仍注释留档，待 14 章建 proto 后恢复
#include "../proto/protoCode/notifyService.pb.h"
// #include "../proto/protoCode/dbService.pb.h"
#include "../common/utils.h"
#include "../common/errorHandler.h"

namespace userService {

UserBusiness::UserBusiness(std::shared_ptr<SessionManager> sessionManager,
                          std::shared_ptr<VerifyCodeData> verifyCodeData,
                          std::shared_ptr<UserData> userData,
                          biterpc::SvcChannels::ptr svcChannels)
    : _sessionManager(sessionManager)
    , _verifyCodeData(verifyCodeData)
    , _userData(userData)
    , _svcChannels(svcChannels) {
}

// 检测用户昵称是否唯一：直接复用数据层 DB 查询
bool UserBusiness::isNicknameUnique(const std::string& nickname) {
    return !_userData->existNicknameInDb(nickname);
}

// 检测用户邮箱是否唯一
bool UserBusiness::isEmailUnique(const std::string& email) {
    return !_userData->existEmailInDb(email);
}

// 注册用户：生成 userId + bcrypt 加密密码 + 落库（不写缓存——Cache-Aside 写路径）
std::string UserBusiness::registerUser(const std::string& nickname,
                                       const std::string& email,
                                       const std::string& password) {
    // 1. 创建UserInfo对象（userId 用 UUID；密码在此处加密——数据层契约1的兑现点）
    UserInfo user;
    user._userId = chat2Data::Utils::generateUuid();
    user._nickname = nickname;
    user._email = email;
    user._password = encryptPassword(password);
    user._status = UserStatus::Offline;
    // 2. 保存用户信息到数据库
    bool saveResult = _userData->saveUserToDb(user);
    if (!saveResult) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::USER_SAVE_USER_INFO_FAILED);
    }

    INF("User registered: userId={}, nickname={}, email={}", user._userId, user._nickname, user._email);
    return user._userId;
}

// 昵称(邮箱)+密码登录：查用户 → 验密码 → 置在线 → 建会话
std::string UserBusiness::loginWithPassword(const std::string& username,
                                           const std::string& password) {
    // 1. 用户通过昵称登录
    std::optional<UserInfo> userOpt = getUserByNickname(username);
    // 2. 用户通过邮箱登录（昵称查不到再按邮箱——"昵称或邮箱当账号"是业务规则）
    if (!userOpt.has_value()) {
        userOpt = getUserByEmail(username);
    }
    if (!userOpt.has_value()) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::USER_NOT_FOUND);
    }
    // 3. 提取用户信息
    UserInfo user = userOpt.value();
    // 4. 密码校验（bcrypt：用 DB 哈希串做盐重新加密比对）
    if (!verifyPassword(password, user._password)) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::USER_LOGIN_FAILED);
    }
    // 5. 更新用户状态为在线
    user._status = UserStatus::Online;
    // 6. 更新MySQL和Redis（存 DB 后删缓存——Cache-Aside 写纪律，让后续读回源拿新状态）
    //    课件问题⑭修复：原版未检查返回值，状态更新失败仍会建会话导致"登录成功却鉴权失败"；
    //    补检查：更新失败即拒绝登录，一致性优先
    if (!_userData->saveUserToDb(user)) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::USER_SAVE_USER_INFO_FAILED);
    }
    _userData->deleteUserCache(user._userId, user._nickname, user._email);
    // 7. 新建会话并返回
    std::string sessionId = _sessionManager->createSession(user._userId);
    INF("User logged in with password: userId={}, sessionId={}", user._userId, sessionId);
    return sessionId;
}

// 生成验证码：生成 codeInfo → 存缓存 → RPC 调通知子服务发邮件（课件问题⑪回收）
// @return codeId
// @throws USER_SAVE_VERIFY_CODE_FAILED / NOTIFY_SEND_FAILED
std::string UserBusiness::getVerifyCode(const std::string& email) {
    // 1. 创建验证码信息（6位纯数字，5分钟有效）
    VerifyCodeInfo codeInfo;
    codeInfo._codeId = chat2Data::Utils::generateUuid();
    codeInfo._email = email;
    codeInfo._verifyCode = biteutil::Random::code(6, biteutil::DIGIT);
    auto now = std::time(nullptr);
    codeInfo._createTime = std::to_string(now);
    // 2. 保存验证码信息到缓存（Redis，TTL 5min，用后即删）
    bool saveResult = _verifyCodeData->saveVerifyCodeToCache(codeInfo);
    if (!saveResult) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::USER_SAVE_VERIFY_CODE_FAILED);
    }
    // 3. 获取 NotifyService 的 channel（服务发现：watcher 已把在线节点灌入）
    auto channel = _svcChannels->getNode(FLAGS_notify_service);
    if (!channel) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::NOTIFY_SEND_FAILED);
    }
    // 4. 创建发送验证码的 rpc 请求
    chat2Data::notifyService::SendVerifyCodeRequest request;
    request.set_request_id(codeInfo._codeId);
    request.set_email(email);
    request.set_code(codeInfo._verifyCode);
    // 5. 创建通知子服务的 rpc 客户端
    chat2Data::notifyService::SendVerifyCodeResponse response;
    brpc::Controller controller;
    chat2Data::notifyService::NotifyService_Stub stub(channel.get());
    // 6. 发起同步 rpc 调用（nullptr=无回调，同步等响应；必须等"任务已受理"才能返回 codeId）
    stub.SendVerifyCode(&controller, &request, &response, nullptr);
    // 7. 检测 rpc 调用是否成功（通信失败 / 业务错误码 分开判）
    if (controller.Failed()) {
        ERR("RPC to NotifyService failed: {}", controller.ErrorText());
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::NOTIFY_SEND_FAILED);
    }
    if (response.error_code() != 0) {
        throw chat2Data::Chat2DataException(static_cast<chat2Data::ErrorCode>(response.error_code()));
    }
    INF("Verification code generated: codeId={}, email={}", codeInfo._codeId, email);
    return codeInfo._codeId;
}

// 验证码登录
std::string UserBusiness::loginWithVerifyCode(const std::string& email,
                                              const std::string& codeId,
                                              const std::string& verifyCode) {
    // 1. 从Redis中获取验证码（codeId 是索引；取不到 = 过期或伪造）
    auto codeInfoOpt = _verifyCodeData->getVerifyCodeFromCache(codeId);
    if (!codeInfoOpt.has_value()) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::USER_VERIFY_CODE_EXPIRED);
    }
    // 2. 提取验证码信息
    const VerifyCodeInfo& codeInfo = codeInfoOpt.value();
    // 3. 验证码信息校验
    //    课件问题⑮修复：原版 codeInfo._codeId != codeInfo._codeId（自比较恒 false），
    //    导致验证码输错也能登录；改为与用户传入参数比对
    if (codeInfo._email != email || codeInfo._verifyCode != verifyCode) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::USER_VERIFY_CODE_ERROR);
    }
    // 4. 通过用户邮箱获取用户信息
    std::optional<UserInfo> userOpt = getUserByEmail(email);
    if (!userOpt.has_value()) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::USER_NOT_FOUND);
    }
    // 5. 提取用户信息结构
    UserInfo user = userOpt.value();
    // 6. 修改用户状态
    user._status = UserStatus::Online;
    // 7. 更新数据库（问题⑭同款修复：检查返回值）
    if (!_userData->saveUserToDb(user)) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::USER_SAVE_USER_INFO_FAILED);
    }
    _userData->deleteUserCache(user._userId, user._nickname, user._email);
    // 8. 新建会话
    std::string sessionId = _sessionManager->createSession(user._userId);
    // 9. 删除Redis中已使用的验证码（用后即删，防重放）
    _verifyCodeData->deleteVerifyCodeFromCache(codeId);
    INF("User logged in with verify code: userId={}, sessionId={}", user._userId, sessionId);
    return sessionId;
}

// 会话登录（续期）：会话有效即视为登录，刷新用户状态
bool UserBusiness::loginWithSession(const std::string& sessionId) {
    // 1. 通过会话Id获取会话信息（无效会话抛 USER_SESSION_INVALID）
    std::string userId = _sessionManager->getUserIdBySessionId(sessionId);
    // 2. 通过会话Id获取用户信息
    std::optional<UserInfo> userOpt = getUserByUserId(userId);
    if (!userOpt.has_value()) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::USER_NOT_FOUND);
    }
    // 3. 从结果中提取用户信息
    UserInfo user = userOpt.value();
    // 4. 更新用户状态为登录
    user._status = UserStatus::Online;
    // 5. 更新MySQL和Redis（问题⑭同款修复）
    if (!_userData->saveUserToDb(user)) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::USER_SAVE_USER_INFO_FAILED);
    }
    _userData->deleteUserCache(user._userId, user._nickname, user._email);
    INF("User logged in with session: userId={}, sessionId={}", user._userId, sessionId);
    return true;
}

// 退出登录：状态离线 → 删会话 → （桩）通知 db 删用户连接
bool UserBusiness::logout(const std::string& sessionId) {
    // 1. 通过会话Id获取用户Id
    std::string userId = _sessionManager->getUserIdBySessionId(sessionId);
    // 2. 通过用户Id获取用户信息
    std::optional<UserInfo> userOpt = getUserByUserId(userId);
    if (!userOpt.has_value()) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::USER_NOT_FOUND);
    }
    // 3. 从查询结果中提取用户信息结构
    UserInfo user = userOpt.value();
    // 4. 修改用户状态为离线
    user._status = UserStatus::Offline;
    // 5. 更新MySQL和Redis（问题⑭同款修复）
    if (!_userData->saveUserToDb(user)) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::USER_SAVE_USER_INFO_FAILED);
    }
    _userData->deleteUserCache(user._userId, user._nickname, user._email);
    // 6. 删除会话（内部抛 USER_SESSION_DELETE_ERROR）
    _sessionManager->deleteSession(sessionId);
    // 7. 删除用户创建的所有数据库连接（桩：等数据库子服务完成）
    deleteUserAllConns(user._userId);

    INF("User logged out: userId={}, sessionId={}", user._userId, sessionId);
    return true;
}

// 获取用户信息
UserInfo UserBusiness::getUserInfo(const std::string& sessionId) {
    // 1. 通过会话id获取用户id
    std::string userId = _sessionManager->getUserIdBySessionId(sessionId);
    INF("Session userId: {}", userId);
    if (userId.empty()) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::USER_SESSION_INVALID);
    }
    // 2. 通过用户id获取用户信息
    std::optional<UserInfo> userOpt = getUserByUserId(userId);
    if (!userOpt.has_value()) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::USER_NOT_FOUND);
    }
    // 3. 结果返回
    INF("Get user info: userId={}", userId);
    return userOpt.value();
}

// 检查会话是否有效（网关鉴权专用）：会话有效 + 用户在线 才算有效
bool UserBusiness::isSessionValid(const std::string& sessionId, std::string& userId) {
    // 1. 通过会话id获取userId
    userId = _sessionManager->getUserIdBySessionId(sessionId);
    // 2. 通过userId获取用户信息
    std::optional<UserInfo> userOpt = getUserByUserId(userId);
    if (!userOpt.has_value()) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::USER_NOT_FOUND);
    }
    // 3. 检查用户状态是否为在线
    return userOpt.value()._status == UserStatus::Online;
}

// Cache-Aside 读路径业务级封装（三镜像，仅数据层方法不同）：
// 缓存优先 → 未命中回源 DB → 命中后回填缓存（TTL 重新计时）
std::optional<UserInfo> UserBusiness::getUserByUserId(const std::string& userId) {
    std::optional<UserInfo> userOpt = _userData->getUserFromCacheByUserId(userId);
    if (!userOpt.has_value()) {
        userOpt = _userData->getUserByUserIdFromDb(userId);
        if (userOpt.has_value()) {
            _userData->saveUserToCache(userOpt.value());
        }
    }
    return userOpt;
}

std::optional<UserInfo> UserBusiness::getUserByNickname(const std::string& nickname) {
    std::optional<UserInfo> userOpt = _userData->getUserFromCacheByNickname(nickname);
    if (!userOpt.has_value()) {
        userOpt = _userData->getUserByNicknameFromDb(nickname);
        if (userOpt.has_value()) {
            _userData->saveUserToCache(userOpt.value());
        }
    }
    return userOpt;
}

std::optional<UserInfo> UserBusiness::getUserByEmail(const std::string& email) {
    std::optional<UserInfo> userOpt = _userData->getUserFromCacheByEmail(email);
    if (!userOpt.has_value()) {
        userOpt = _userData->getUserByEmailFromDb(email);
        if (userOpt.has_value()) {
            _userData->saveUserToCache(userOpt.value());
        }
    }
    return userOpt;
}

// 密码加密（bcrypt $2b$10$，随机 16 字节盐）
std::string UserBusiness::encryptPassword(const std::string& password) {
    // 1. 生成随机盐值（16 字节，thread_local mt19937：每线程一个引擎，避免锁竞争）
    std::random_device rd;
    thread_local static std::mt19937 gen(rd());
    thread_local static std::uniform_int_distribution<int> dis(0, 255);
    unsigned char saltBytes[16];
    for (int i = 0; i < 16; ++i) {
        saltBytes[i] = static_cast<unsigned char>(dis(gen));
    }
    // 2. bcrypt 专用字母表编码（问题⑬修复：标准 base64 的 '+'/'=' 是非法盐字符）
    std::string saltBase64 = chat2Data::Utils::bcryptSaltEncode(
        std::vector<char>(reinterpret_cast<char*>(saltBytes), reinterpret_cast<char*>(saltBytes + 16)));
    // 3. 在盐值上拼接 $2b$10$（bcrypt cost=10），取前 22 字符构成合法盐设置串
    std::string saltSetting = "$2b$10$" + saltBase64.substr(0, 22);
    // 4. 对密码进行加密（crypt_r 线程安全版；'*' 开头表示 crypt 内部错误）
    struct crypt_data data;
    memset(&data, 0, sizeof(data));
    char* hashed = crypt_r(password.c_str(), saltSetting.c_str(), &data);
    if (hashed == nullptr || hashed[0] == '*') {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::USER_PASSWORD_ENCRYPT_ERROR);
    }
    return std::string(hashed);
}

// 验证密码：用 DB 中哈希串整体作为盐重新加密，比较结果串是否一致
// （bcrypt 哈希串自带盐+cost，"同盐重加密"结果必然相同——前提是密码正确）
bool UserBusiness::verifyPassword(const std::string& password, const std::string& encrypted) {
    struct crypt_data data;
    memset(&data, 0, sizeof(data));
    // 使用用户输入的密码 + 数据库中加密之后的哈希字符串来重新加密
    char* hashed = crypt_r(password.c_str(), encrypted.c_str(), &data);
    if (hashed == nullptr || hashed[0] == '*') {
        return false;
    }
    // 重新加密结果与库中哈希串直接比较（恒定时间比较更佳，教学版沿用 strcmp）
    return strcmp(hashed, encrypted.c_str()) == 0;
}

// 删除用户创建的所有数据库连接（桩：等数据库子服务完成）
void UserBusiness::deleteUserAllConns(const std::string& userId) {
    // ...
    // 等数据库子服务实现完成之后再完善
}

} // namespace userService

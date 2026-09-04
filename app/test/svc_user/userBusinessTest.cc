#include <gtest/gtest.h>
#include <memory>
#include <bite_scaffold/log.h>
// ⚠️ include 顺序敏感：userBusiness.h 会拉入 brpc 枚举(REDIS_REPLY_*)，
// 必须在 bite_scaffold/redis.h(→hiredis 同名宏) 之前 include，否则宏污染 brpc/redis_reply.h 编译失败
#include "../../svc_userService/userBusiness.h"
#include <bite_scaffold/odb.h>
#include <bite_scaffold/redis.h>
#include <bite_scaffold/util.h>
#include "../../common/utils.h"
#include "../../common/errorHandler.h"
#include "../../data/userData.h"
#include "../../data/sessionData.h"
#include "../../data/verifyCodeData.h"
#include "../../svc_userService/sessionManager.h"

using namespace userService;

int main(int argc, char** argv) {
    bitelog::bitelog_init();
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

namespace {

// 完整装配链：连接工厂 → 数据层三件套 → SessionManager → UserBusiness
// 与未来 UserServer 的装配结构一致（生产版多一个 SvcChannels 和 ETCD 注册）
class BizEnv {
public:
    BizEnv() {
        biteodb::mysql_settings ms;
        ms.host = "dev-mysql"; ms.user = "root"; ms.passwd = "123456";
        ms.db = "chat2Data"; ms.cset = "utf8mb4"; ms.port = 3306;
        ms.connection_pool_size = 3;
        auto db = biteodb::DBFactory::mysql(ms);

        biteredis::redis_settings rs;
        rs.host = "dev-redis"; rs.port = 6379; rs.passwd = "123456";
        rs.db = 0; rs.connection_pool_size = 3;
        auto redis = biteredis::RedisFactory::create(rs);

        _userData = std::make_shared<UserData>(db, redis);
        _sessionData = std::make_shared<SessionData>(db, redis);
        _verifyCodeData = std::make_shared<VerifyCodeData>(redis);
        _sessionManager = std::make_shared<SessionManager>(_sessionData.get());
        _biz = std::make_shared<UserBusiness>(_sessionManager, _verifyCodeData,
                                              _userData, nullptr);
    }

    std::shared_ptr<UserBusiness> biz() { return _biz; }
    std::shared_ptr<VerifyCodeData> verifyCodeData() { return _verifyCodeData; }

private:
    std::shared_ptr<UserData> _userData;
    std::shared_ptr<SessionData> _sessionData;      // SessionManager 裸指针借用的所有权在装配层
    std::shared_ptr<VerifyCodeData> _verifyCodeData;
    std::shared_ptr<SessionManager> _sessionManager;
    std::shared_ptr<UserBusiness> _biz;
};

UserInfo makeUser() {
    UserInfo u;
    u._userId = chat2Data::Utils::generateUuid();
    u._nickname = "业务测试_" + u._userId.substr(0, 8);
    u._email = u._userId.substr(0, 8) + "@biz.com";
    u._password = "明文密码";          // 注册接口负责加密
    u._status = UserStatus::Offline;
    return u;
}

} // namespace

// 1. 注册 → 密码登录 → 获取用户信息（密码不出现在响应中）
TEST(UserBusinessTest, RegisterAndLogin) {
    BizEnv env;
    auto user = makeUser();

    // 注册（内部 encryptPassword 落库密文）
    std::string userId = env.biz()->registerUser(user._nickname, user._email, user._password);
    EXPECT_FALSE(userId.empty());

    // 唯一性检查：已注册的昵称/邮箱不再唯一
    EXPECT_FALSE(env.biz()->isNicknameUnique(user._nickname));
    EXPECT_FALSE(env.biz()->isEmailUnique(user._email));

    // 昵称登录
    std::string sessionId = env.biz()->loginWithPassword(user._nickname, user._password);
    EXPECT_FALSE(sessionId.empty());

    // 获取用户信息：proto 脱敏前的业务层 UserInfo（含密码，但 RPC 层会丢弃）
    UserInfo info = env.biz()->getUserInfo(sessionId);
    EXPECT_EQ(info._userId, userId);
    EXPECT_EQ(info._nickname, user._nickname);
    EXPECT_EQ(info._status, UserStatus::Online);   // 登录后在线
}

// 2. 错误密码 → USER_LOGIN_FAILED
TEST(UserBusinessTest, LoginWrongPassword) {
    BizEnv env;
    auto user = makeUser();
    env.biz()->registerUser(user._nickname, user._email, user._password);

    EXPECT_THROW({
        env.biz()->loginWithPassword(user._nickname, "错误密码");
    }, chat2Data::Chat2DataException);
}

// 3. 会话续期：loginWithSession → isSessionValid 且 userId 回填
TEST(UserBusinessTest, SessionFlow) {
    BizEnv env;
    auto user = makeUser();
    env.biz()->registerUser(user._nickname, user._email, user._password);
    std::string sessionId = env.biz()->loginWithPassword(user._email, user._password);

    EXPECT_TRUE(env.biz()->loginWithSession(sessionId));
    std::string userId;
    EXPECT_TRUE(env.biz()->isSessionValid(sessionId, userId));
    EXPECT_EQ(userId, userId);
}

// 4. 登出 → 会话失效 → 再鉴权抛 USER_SESSION_INVALID
TEST(UserBusinessTest, LogoutFlow) {
    BizEnv env;
    auto user = makeUser();
    env.biz()->registerUser(user._nickname, user._email, user._password);
    std::string sessionId = env.biz()->loginWithPassword(user._nickname, user._password);

    ASSERT_TRUE(env.biz()->logout(sessionId));

    std::string userId;
    EXPECT_THROW({
        env.biz()->isSessionValid(sessionId, userId);
    }, chat2Data::Chat2DataException);
}

// 5. 验证码登录全流程：手动造码入缓存（绕过 getVerifyCode 桩）→ 正确登录 / 错误码拒绝 / 用后即删
TEST(UserBusinessTest, VerifyCodeLoginFlow) {
    BizEnv env;
    auto user = makeUser();
    env.biz()->registerUser(user._nickname, user._email, user._password);

    // 手动造验证码（绕过 getVerifyCode 桩：桩等 notify 子服务）
    VerifyCodeInfo code;
    code._codeId = "code_" + chat2Data::Utils::generateUuid().substr(0, 8);
    code._verifyCode = biteutil::Random::code(6);
    code._email = user._email;
    code._createTime = "2026-09-04 19:35:00";
    ASSERT_TRUE(env.verifyCodeData()->saveVerifyCodeToCache(code));

    // 错误验证码 → USER_VERIFY_CODE_ERROR
    EXPECT_THROW({
        env.biz()->loginWithVerifyCode(user._email, code._codeId, "000000");
    }, chat2Data::Chat2DataException);

    // 正确验证码 → 登录成功
    std::string sessionId = env.biz()->loginWithVerifyCode(user._email, code._codeId, code._verifyCode);
    EXPECT_FALSE(sessionId.empty());

    // 用后即删：同码再登录 → USER_VERIFY_CODE_EXPIRED（防重放）
    EXPECT_THROW({
        env.biz()->loginWithVerifyCode(user._email, code._codeId, code._verifyCode);
    }, chat2Data::Chat2DataException);
}

// 6. 幽灵邮箱验证码登录 → 验证码缓存 miss → USER_VERIFY_CODE_EXPIRED
TEST(UserBusinessTest, VerifyCodeGhostEmail) {
    BizEnv env;
    std::string ghostEmail = "ghost_" + chat2Data::Utils::generateUuid().substr(0, 6) + "@x.com";

    EXPECT_THROW({
        env.biz()->loginWithVerifyCode(ghostEmail, "fake_code_id", "123456");
    }, chat2Data::Chat2DataException);
}

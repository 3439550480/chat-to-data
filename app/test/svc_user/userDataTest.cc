#include <gtest/gtest.h>
#include <memory>
#include <bite_scaffold/log.h>
#include <bite_scaffold/odb.h>
#include <bite_scaffold/redis.h>
#include "../../common/utils.h"
#include "../../data/userData.h"
#include "../../svc_userService/common.h"

using namespace userService;

// 主函数：先初始化 bite 日志系统（INF/WRN 宏依赖 g_logger，不 init 会段错误），再跑 gtest
int main(int argc, char** argv) {
    bitelog::bitelog_init();   // 默认输出到 stdout
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

namespace {

// 问题⑧ 修复的装配逻辑落点：config 字段映射 → scaffold settings → 工厂创建连接 → 注入 UserData
// 课件测试原版用 192.168.150.129(虚拟机) 且直接 (MYSQL_CONFIG, REDIS_CONFIG) 构造 UserData(编译不过)
// 本版：连接指向容器网络 dev-mysql/dev-redis，装配走依赖注入(方案A)
std::shared_ptr<UserData> createUserData() {
    biteodb::mysql_settings ms;
    ms.host = "dev-mysql"; ms.user = "root"; ms.passwd = "123456";
    ms.db = "chat2Data"; ms.cset = "utf8mb4"; ms.port = 3306;
    ms.connection_pool_size = 3;
    auto db = biteodb::DBFactory::mysql(ms);

    biteredis::redis_settings rs;
    rs.host = "dev-redis"; rs.port = 6379; rs.passwd = "123456";
    rs.db = 0; rs.connection_pool_size = 3;
    auto redis = biteredis::RedisFactory::create(rs);

    return std::make_shared<UserData>(db, redis);
}

// 每次运行生成全新身份，避免上次残留数据导致 UNIQUE 冲突（测试可重复执行）
UserInfo makeUser() {
    UserInfo u;
    u._userId = chat2Data::Utils::generateUuid();
    u._nickname = "测试_" + u._userId.substr(0, 8);
    u._email = u._userId.substr(0, 8) + "@test.com";
    u._password = "加密密文$2y$10$abc";   // 契约1：传密文，测数据层不关心加密算法
    u._status = UserStatus::Offline;
    return u;
}

} // namespace

// 1. DB 插入 + 三键查询
TEST(UserDataTest, SaveAndQueryFromDb) {
    auto data = createUserData();
    auto user = makeUser();

    ASSERT_TRUE(data->saveUserToDb(user));

    auto byId = data->getUserByUserIdFromDb(user._userId);
    ASSERT_TRUE(byId.has_value());
    EXPECT_EQ(byId->_nickname, user._nickname);
    EXPECT_EQ(byId->_email, user._email);
    EXPECT_EQ(byId->_password, user._password);
    EXPECT_EQ(byId->_status, UserStatus::Offline);

    auto byNick = data->getUserByNicknameFromDb(user._nickname);
    ASSERT_TRUE(byNick.has_value());
    EXPECT_EQ(byNick->_userId, user._userId);

    auto byEmail = data->getUserByEmailFromDb(user._email);
    ASSERT_TRUE(byEmail.has_value());
    EXPECT_EQ(byEmail->_userId, user._userId);
}

// 2. 存在性检查（昵称/邮箱）
TEST(UserDataTest, ExistsCheck) {
    auto data = createUserData();
    auto user = makeUser();
    data->saveUserToDb(user);

    EXPECT_TRUE(data->existNicknameInDb(user._nickname));
    EXPECT_TRUE(data->existEmailInDb(user._email));
    EXPECT_FALSE(data->existNicknameInDb("绝不存在的昵称_" + user._userId));
}

// 3. update 路径：同 userId 二次保存 → 更新而非插入
TEST(UserDataTest, UpdateExistingUser) {
    auto data = createUserData();
    auto user = makeUser();
    ASSERT_TRUE(data->saveUserToDb(user));

    user._nickname = "改名_" + user._userId.substr(0, 8);
    user._status = UserStatus::Online;
    ASSERT_TRUE(data->saveUserToDb(user));

    auto byId = data->getUserByUserIdFromDb(user._userId);
    ASSERT_TRUE(byId.has_value());
    EXPECT_EQ(byId->_nickname, user._nickname);    // 昵称已更新
    EXPECT_EQ(byId->_status, UserStatus::Online);  // 状态已更新
}

// 4. 缓存三键写/读/删（Cache-Aside 全链路）
TEST(UserDataTest, CacheWriteReadDelete) {
    auto data = createUserData();
    auto user = makeUser();
    data->saveUserToDb(user);
    ASSERT_TRUE(data->saveUserToCache(user));

    auto byId = data->getUserFromCacheByUserId(user._userId);
    ASSERT_TRUE(byId.has_value());
    EXPECT_EQ(byId->_nickname, user._nickname);   // 中文昵称 JSON 往返一致

    auto byNick = data->getUserFromCacheByNickname(user._nickname);
    ASSERT_TRUE(byNick.has_value());
    EXPECT_EQ(byNick->_userId, user._userId);

    auto byEmail = data->getUserFromCacheByEmail(user._email);
    ASSERT_TRUE(byEmail.has_value());
    EXPECT_EQ(byEmail->_userId, user._userId);

    // 删除三键后全部未命中
    data->deleteUserCache(user._userId, user._nickname, user._email);
    EXPECT_FALSE(data->getUserFromCacheByUserId(user._userId).has_value());
    EXPECT_FALSE(data->getUserFromCacheByNickname(user._nickname).has_value());
    EXPECT_FALSE(data->getUserFromCacheByEmail(user._email).has_value());
}

// 5. 查无此人/未命中 → nullopt
TEST(UserDataTest, NotFoundReturnsNullopt) {
    auto data = createUserData();
    std::string ghost = "ghost_" + chat2Data::Utils::generateUuid().substr(0, 8);

    EXPECT_FALSE(data->getUserByUserIdFromDb(ghost).has_value());
    EXPECT_FALSE(data->getUserFromCacheByUserId(ghost).has_value());
}

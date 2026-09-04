#include <gtest/gtest.h>
#include <memory>
#include <bite_scaffold/log.h>
#include <bite_scaffold/odb.h>
#include <bite_scaffold/redis.h>
#include "../../common/utils.h"
#include "../../data/sessionData.h"
#include "../../svc_userService/common.h"

using namespace userService;

// 同 userDataTest：先初始化日志系统（INF 宏依赖 g_logger）
int main(int argc, char** argv) {
    bitelog::bitelog_init();
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

namespace {

std::shared_ptr<SessionData> createSessionData() {
    biteodb::mysql_settings ms;
    ms.host = "dev-mysql"; ms.user = "root"; ms.passwd = "123456";
    ms.db = "chat2Data"; ms.cset = "utf8mb4"; ms.port = 3306;
    ms.connection_pool_size = 3;
    auto db = biteodb::DBFactory::mysql(ms);

    biteredis::redis_settings rs;
    rs.host = "dev-redis"; rs.port = 6379; rs.passwd = "123456";
    rs.db = 0; rs.connection_pool_size = 3;
    auto redis = biteredis::RedisFactory::create(rs);

    return std::make_shared<SessionData>(db, redis);
}

SessionInfo makeSession() {
    SessionInfo s;
    s._sessionId = "sess_" + chat2Data::Utils::generateUuid().substr(0, 8);
    s._userId = "user_" + chat2Data::Utils::generateUuid().substr(0, 8);
    return s;
}

} // namespace

// 1. DB 插入 + 查询
TEST(SessionDataTest, SaveAndGetFromDb) {
    auto data = createSessionData();
    auto sess = makeSession();

    ASSERT_TRUE(data->saveSessionToDb(sess));
    auto got = data->getSessionBySessionIdFromDb(sess._sessionId);
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->_sessionId, sess._sessionId);
    EXPECT_EQ(got->_userId, sess._userId);
}

// 2. DB 删除后查无此会话
TEST(SessionDataTest, DeleteFromDb) {
    auto data = createSessionData();
    auto sess = makeSession();
    ASSERT_TRUE(data->saveSessionToDb(sess));
    ASSERT_TRUE(data->deleteSessionFromDb(sess._sessionId));
    EXPECT_FALSE(data->getSessionBySessionIdFromDb(sess._sessionId).has_value());
}

// 3. 缓存写/读/删（幂等删除：删两次也返回 true）
TEST(SessionDataTest, CacheWriteReadDelete) {
    auto data = createSessionData();
    auto sess = makeSession();
    ASSERT_TRUE(data->saveSessionToCache(sess));

    auto got = data->getSessionFromCache(sess._sessionId);
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->_userId, sess._userId);

    ASSERT_TRUE(data->deleteSessionFromCache(sess._sessionId));
    EXPECT_FALSE(data->getSessionFromCache(sess._sessionId).has_value());
    // 幂等：再删一次仍返回 true
    EXPECT_TRUE(data->deleteSessionFromCache(sess._sessionId));
}

// 4. 幽灵会话 → nullopt
TEST(SessionDataTest, NotFoundReturnsNullopt) {
    auto data = createSessionData();
    std::string ghost = "ghost_" + chat2Data::Utils::generateUuid().substr(0, 8);
    EXPECT_FALSE(data->getSessionBySessionIdFromDb(ghost).has_value());
    EXPECT_FALSE(data->getSessionFromCache(ghost).has_value());
}

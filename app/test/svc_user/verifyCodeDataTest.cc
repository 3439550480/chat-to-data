#include <gtest/gtest.h>
#include <memory>
#include <bite_scaffold/log.h>
#include <bite_scaffold/odb.h>
#include <bite_scaffold/redis.h>
#include <bite_scaffold/util.h>
#include "../../common/utils.h"
#include "../../data/verifyCodeData.h"

using namespace userService;

int main(int argc, char** argv) {
    bitelog::bitelog_init();
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

namespace {

std::shared_ptr<VerifyCodeData> createVerifyCodeData() {
    biteredis::redis_settings rs;
    rs.host = "dev-redis"; rs.port = 6379; rs.passwd = "123456";
    rs.db = 0; rs.connection_pool_size = 3;
    auto redis = biteredis::RedisFactory::create(rs);
    return std::make_shared<VerifyCodeData>(redis);
}

VerifyCodeInfo makeCode() {
    VerifyCodeInfo c;
    c._codeId = "code_" + chat2Data::Utils::generateUuid().substr(0, 8);
    c._verifyCode = biteutil::Random::code(6);          // 6 位随机验证码
    c._email = c._codeId.substr(5) + "@test.com";
    c._createTime = "2026-09-03 16:00:00";
    return c;
}

} // namespace

// 1. 保存 + 读取往返（4 字段校验）
TEST(VerifyCodeDataTest, SaveAndRead) {
    auto data = createVerifyCodeData();
    auto code = makeCode();

    ASSERT_TRUE(data->saveVerifyCodeToCache(code));

    auto got = data->getVerifyCodeFromCache(code._codeId);
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->_codeId, code._codeId);
    EXPECT_EQ(got->_verifyCode, code._verifyCode);
    EXPECT_EQ(got->_email, code._email);
    EXPECT_EQ(got->_createTime, code._createTime);
}

// 2. 用后即删 + 二次删除返回 false（用后即删语义）
TEST(VerifyCodeDataTest, DeleteAfterUse) {
    auto data = createVerifyCodeData();
    auto code = makeCode();
    data->saveVerifyCodeToCache(code);

    EXPECT_TRUE(data->deleteVerifyCodeFromCache(code._codeId));    // 首删成功
    EXPECT_FALSE(data->getVerifyCodeFromCache(code._codeId).has_value());
    EXPECT_FALSE(data->deleteVerifyCodeFromCache(code._codeId));   // 再删 = 验证码已用/过期信号
}

// 3. 幽灵 codeId → nullopt
TEST(VerifyCodeDataTest, NotFoundReturnsNullopt) {
    auto data = createVerifyCodeData();
    std::string ghost = "code_ghost_" + chat2Data::Utils::generateUuid().substr(0, 8);
    EXPECT_FALSE(data->getVerifyCodeFromCache(ghost).has_value());
}

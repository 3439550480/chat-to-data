// H15a：聊天会话数据层 + 管理器单测（真 MySQL + Redis，进程内）
// 覆盖：保存/查询全字段、更新语义、缓存往返（fileId NULL 链路 + 中文）、
//       缓存删除、用户隔离与归属校验、关联文件与删除权限
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>
#include <odb/transaction.hxx>
#include <bite_scaffold/log.h>
#include <bite_scaffold/odb.h>
#include <bite_scaffold/redis.h>
#include "../../../data/chatSessionData.h"
#include "../../../svc_aiService/chatSessionMgr.h"
#include "../../../common/utils.h"

int main(int argc, char** argv) {
    bitelog::bitelog_init();
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

class ChatSessionTest : public ::testing::Test {
protected:
    void SetUp() override {
        biteodb::mysql_settings ms;
        ms.host = "dev-mysql";
        ms.user = "root";
        ms.passwd = "123456";
        ms.db = "chat2Data";
        ms.port = 3306;
        ms.cset = "utf8mb4";
        auto db = biteodb::DBFactory::mysql(ms);
        ASSERT_NE(db, nullptr);
        // 用 db=1 隔离测试缓存，避免与运行中服务的 db0 数据互扰
        biteredis::redis_settings rs;
        rs.host = "dev-redis";
        rs.port = 6379;
        rs.passwd = "123456";
        rs.db = 1;
        rs.connection_pool_size = 3;
        auto redis = biteredis::RedisFactory::create(rs);
        ASSERT_NE(redis, nullptr);
        _db = db;
        _data = std::make_shared<aiService::ChatSessionData>(db, redis);
        _mgr = std::make_shared<aiService::ChatSessionMgr>(_data);
    }

    void TearDown() override {
        // 清理本测试创建的会话（DB + 缓存）
        for (const auto& id : _createdIds) {
            _data->deleteChatSessionBySessionIdFromDb(id);
            _data->deleteChatSessionFromCache(id);
        }
        // 清理测试用文件行（会话已删，无级联影响）
        if (_db) {
            odb::transaction trans(_db->begin());
            _db->execute("DELETE FROM tbl_fileInfo WHERE userId = 'user_assoc'");
            trans.commit();
        }
    }

    // 构造一份会话信息（title 支持中文，验证 utf8mb4 链路）
    aiService::ChatSessionInfo makeInfo(const std::string& sessionId,
                                        const std::string& userId,
                                        const std::string& title = "",
                                        const std::string& fileId = "",
                                        const std::string& sessionType = "plain") {
        aiService::ChatSessionInfo info;
        info._chatSessionId = sessionId;
        info._userId = userId;
        info._title = title;
        info._createTime = 1789000000;
        info._updateTime = 1789000000;
        info._messageCount = 0;
        info._modelName = "glm-5.3-flash";
        info._fileId = fileId;
        info._sessionType = sessionType;
        info._dbConnectionInfo = "";
        return info;
    }

    std::string uniqueId(const std::string& prefix) {
        return prefix + "_" + chat2Data::Utils::generateUuid().substr(0, 8);
    }

    std::shared_ptr<aiService::ChatSessionData> _data;
    std::shared_ptr<aiService::ChatSessionMgr> _mgr;
    std::shared_ptr<odb::database> _db;
    std::vector<std::string> _createdIds;
};

// 1. 保存 + 按会话 Id 查询：全字段往返（含中文 title/model）
TEST_F(ChatSessionTest, SaveAndGetById) {
    std::string id = uniqueId("cs_save");
    _createdIds.push_back(id);
    auto info = makeInfo(id, "user_save", "部门薪资统计", "", "plain");
    info._modelName = "glm-5.3-flash";
    ASSERT_TRUE(_mgr->saveChatSession(info));

    auto got = _mgr->getChatSessionBySessionId(id);
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->_chatSessionId, id);
    EXPECT_EQ(got->_userId, "user_save");
    EXPECT_EQ(got->_title, "部门薪资统计");
    EXPECT_EQ(got->_modelName, "glm-5.3-flash");
    EXPECT_EQ(got->_sessionType, "plain");
    EXPECT_EQ(got->_fileId, "");                 // 未关联 → 空串（DB NULL）
    EXPECT_EQ(got->_createTime, 1789000000);
    EXPECT_EQ(got->_messageCount, 0);
}

// 2. 更新语义：同 Id 存两次 → 走"存在则更新"路径，字段生效
TEST_F(ChatSessionTest, UpdateSemantics) {
    std::string id = uniqueId("cs_upd");
    _createdIds.push_back(id);
    ASSERT_TRUE(_mgr->saveChatSession(makeInfo(id, "user_upd")));
    auto updated = makeInfo(id, "user_upd", "新标题", "", "plain");
    updated._messageCount = 4;
    updated._updateTime = 1789000500;
    ASSERT_TRUE(_mgr->saveChatSession(updated));

    auto got = _mgr->getChatSessionBySessionId(id);
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->_title, "新标题");
    EXPECT_EQ(got->_messageCount, 4);
    EXPECT_EQ(got->_updateTime, 1789000500);
    // 仍只有一行（更新而非插入）
    auto list = _mgr->getChatSessionsByUserId("user_upd");
    EXPECT_EQ(list.size(), 1u);
}

// 3. 缓存往返：JSON 整存整取；fileId 空串→NULL→空串；中文无损
TEST_F(ChatSessionTest, CacheRoundTrip) {
    std::string id = uniqueId("cs_cache");
    auto entity = ChatSessionEntity(
        id, "user_cache", "缓存往返测试", 1789000000, 1789000000, 2,
        "glm-5.3-flash", "", "plain", "");
    ASSERT_TRUE(_data->saveChatSessionToCache(entity));
    auto got = _data->getChatSessionFromCacheBySessionId(id);
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->chatSessionId(), id);
    EXPECT_EQ(got->title(), "缓存往返测试");
    EXPECT_EQ(got->fileId(), "");               // 空串 → reset → 读回空串
    EXPECT_EQ(got->messageCount(), 2);

    // 关联文件的会话：fileId 有值 → 缓存往返保真
    ChatSessionEntity withFile(
        id, "user_cache", "带文件", 1789000000, 1789000000, 4,
        "glm-5.3-flash", "file_abc123", "excel", "");
    ASSERT_TRUE(_data->saveChatSessionToCache(withFile));
    auto got2 = _data->getChatSessionFromCacheBySessionId(id);
    ASSERT_TRUE(got2.has_value());
    EXPECT_EQ(got2->fileId(), "file_abc123");
    _data->deleteChatSessionFromCache(id);
}

// 4. 缓存删除：删除后再读 → nullopt
TEST_F(ChatSessionTest, CacheDelete) {
    std::string id = uniqueId("cs_cdel");
    auto entity = ChatSessionEntity(
        id, "user_cdel", "", 1789000000, 1789000000, 0, "glm-5.3-flash",
        "", "plain", "");
    ASSERT_TRUE(_data->saveChatSessionToCache(entity));
    _data->deleteChatSessionFromCache(id);
    EXPECT_FALSE(_data->getChatSessionFromCacheBySessionId(id).has_value());
}

// 5. 用户隔离：各自列表互不可见；归属校验 true/false/不存在
TEST_F(ChatSessionTest, UserIsolation) {
    std::string idA = uniqueId("cs_iso_a");
    std::string idB = uniqueId("cs_iso_b");
    _createdIds.push_back(idA);
    _createdIds.push_back(idB);
    ASSERT_TRUE(_mgr->saveChatSession(makeInfo(idA, "user_A")));
    ASSERT_TRUE(_mgr->saveChatSession(makeInfo(idB, "user_B")));

    auto listA = _mgr->getChatSessionsByUserId("user_A");
    ASSERT_EQ(listA.size(), 1u);
    EXPECT_EQ(listA[0]._chatSessionId, idA);
    auto listB = _mgr->getChatSessionsByUserId("user_B");
    ASSERT_EQ(listB.size(), 1u);
    EXPECT_EQ(listB[0]._chatSessionId, idB);

    EXPECT_TRUE(_mgr->isSessionOwnedByUser(idA, "user_A"));
    EXPECT_FALSE(_mgr->isSessionOwnedByUser(idA, "user_B"));
    EXPECT_FALSE(_mgr->isSessionOwnedByUser("cs_not_exist_1234", "user_A"));
}

// 6. 关联文件 + 删除权限
// 附带 FK 完整性实测：fileId 必须是 tbl_fileInfo 里真实存在的行
//（H3 外键决策的正确性证明——不存在的文件 ID 会被 1452 拒绝）
TEST_F(ChatSessionTest, AssociateAndDeletePermission) {
    std::string id = uniqueId("cs_assoc");
    _createdIds.push_back(id);
    // 先插入真实文件行（关联的前提；ODB 裸 execute 须在事务内）
    {
        odb::transaction trans(_db->begin());
        ASSERT_TRUE(_db->execute(
            "INSERT INTO tbl_fileInfo (fileId, fileName, fileExt, fileSize, uploadTime, "
            "fdfsFileId, userId, chatSessionId) VALUES "
            "('file_xyz789', '关联测试.xlsx', '.xlsx', 100, 1789000000, '', 'user_assoc', '')"));
        trans.commit();
    }
    ASSERT_TRUE(_mgr->saveChatSession(makeInfo(id, "user_assoc")));

    // 先验证 FK 拒绝幽灵文件 ID
    EXPECT_FALSE(_mgr->associateFileId(id, "file_not_exist", "user_assoc"));

    // 关联文件（正确用户；fileId 真实存在 → FK 通过）
    EXPECT_TRUE(_mgr->associateFileId(id, "file_xyz789", "user_assoc"));
    auto got = _mgr->getChatSessionBySessionId(id);
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->_fileId, "file_xyz789");

    // 错误用户关联 → 拒绝
    EXPECT_FALSE(_mgr->associateFileId(id, "file_other", "wrong_user"));
    // 错误用户删除 → 拒绝
    EXPECT_FALSE(_mgr->deleteChatSession(id, "wrong_user"));
    EXPECT_TRUE(_mgr->getChatSessionBySessionId(id).has_value());
    // 正确用户删除 → 成功且 DB/缓存均无
    EXPECT_TRUE(_mgr->deleteChatSession(id, "user_assoc"));
    EXPECT_FALSE(_data->getChatSessionBySessionIdFromDb(id).has_value());
    EXPECT_FALSE(_data->getChatSessionFromCacheBySessionId(id).has_value());
}

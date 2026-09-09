#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <bite_scaffold/log.h>
#include <bite_scaffold/odb.h>
#include <bite_scaffold/redis.h>
#include "../../data/fileData.h"
#include "../../data/worksheetData.h"
#include "../../data/fileEntity.h"
#include "../../data/worksheetEntity.h"
#include "../../common/utils.h"

using namespace fileService;

int main(int argc, char** argv) {
    bitelog::bitelog_init();
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

// 测试环境：真 MySQL + Redis（同 userDataTest 模式），每测试用唯一 fileId 隔离
class FileDataTest : public ::testing::Test {
protected:
    void SetUp() override {
        biteodb::mysql_settings ms;
        ms.host = "dev-mysql"; ms.user = "root"; ms.passwd = "123456";
        ms.db = "chat2Data"; ms.cset = "utf8mb4"; ms.port = 3306;
        ms.connection_pool_size = 3;
        auto db = biteodb::DBFactory::mysql(ms);

        biteredis::redis_settings rs;
        rs.host = "dev-redis"; rs.port = 6379; rs.passwd = "123456";
        rs.db = 0; rs.connection_pool_size = 3;
        auto redis = biteredis::RedisFactory::create(rs);

        _fileData = std::make_shared<FileInfoData>(db, redis);
        _worksheetData = std::make_shared<WorksheetData>(db, redis);
        _fileId = "ftest_" + chat2Data::Utils::generateUuid().substr(0, 8);
    }
    void TearDown() override {
        // 兜底清理：DB 行 + 缓存键（测试内已删的幂等重删无害）
        _fileData->deleteFileInfoByFileIdFromDb(_fileId);
        _fileData->deleteFileInfoCache(_fileId);
        _worksheetData->deleteWorksheetMappingsByFileIdFromDb(_fileId);
        _worksheetData->deleteWorksheetMappingsCache(_fileId);
    }
    // 构造一个测试元数据
    FileInfoEntity makeEntity(const std::string& fdfsFileId = "") {
        return FileInfoEntity(_fileId, "测试文件.xlsx", ".xlsx", 10240,
                              static_cast<int64_t>(::time(nullptr)),
                              fdfsFileId, "user_ftest", "");
    }
    std::shared_ptr<FileInfoData> _fileData;
    std::shared_ptr<WorksheetData> _worksheetData;
    std::string _fileId;
};

// 1. 元数据保存 + 双路径查询（fileId / fileId+userId）
TEST_F(FileDataTest, SaveAndQuery) {
    auto entity = makeEntity();
    // 保存
    EXPECT_TRUE(_fileData->saveFileInfoToDb(entity));
    // fileId 查询
    auto byId = _fileData->getFileInfoByFileIdFromDb(_fileId);
    ASSERT_TRUE(byId.has_value());
    EXPECT_EQ(byId->fileName(), "测试文件.xlsx");
    EXPECT_EQ(byId->userId(), "user_ftest");
    // fileId+userId 联合查询：对的主人 → 命中
    auto owned = _fileData->getFileInfoByFileIdAndUserIdFromDb(_fileId, "user_ftest");
    EXPECT_TRUE(owned.has_value());
    // 联合查询：错的主人 → nullopt（归属校验的 DB 半边）
    auto intruder = _fileData->getFileInfoByFileIdAndUserIdFromDb(_fileId, "user_other");
    EXPECT_FALSE(intruder.has_value());
}

// 2. 保存或更新语义：同 fileId 再存 → 更新而非新行
TEST_F(FileDataTest, SaveOrUpdate) {
    EXPECT_TRUE(_fileData->saveFileInfoToDb(makeEntity()));
    // 同 fileId 更新 fdfsFileId（模拟 UploadFile 第二段的元数据回填）
    auto updated = makeEntity("fdfs_test_abc");
    EXPECT_TRUE(_fileData->saveFileInfoToDb(updated));
    auto result = _fileData->getFileInfoByFileIdFromDb(_fileId);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->fdfsFileId(), "fdfs_test_abc");
}

// 3. 缓存三段：写入 → 命中回读（JSON 往返无损） → 删除后 miss
TEST_F(FileDataTest, CacheRoundTrip) {
    auto entity = makeEntity("fdfs_cache_1");
    // 未写入缓存前 miss
    EXPECT_FALSE(_fileData->getFileInfoFromCacheByFileId(_fileId).has_value());
    // 写入缓存
    EXPECT_TRUE(_fileData->saveFileInfoToCache(entity));
    // 命中且字段无损
    auto cached = _fileData->getFileInfoFromCacheByFileId(_fileId);
    ASSERT_TRUE(cached.has_value());
    EXPECT_EQ(cached->fileId(), _fileId);
    EXPECT_EQ(cached->fileName(), "测试文件.xlsx");
    EXPECT_EQ(cached->fdfsFileId(), "fdfs_cache_1");
    EXPECT_EQ(cached->userId(), "user_ftest");
    // 删除缓存后再次 miss
    _fileData->deleteFileInfoCache(_fileId);
    EXPECT_FALSE(_fileData->getFileInfoFromCacheByFileId(_fileId).has_value());
}

// 4. 元数据删除：删后 DB 与缓存均不可见
TEST_F(FileDataTest, DeleteFileInfo) {
    EXPECT_TRUE(_fileData->saveFileInfoToDb(makeEntity("fdfs_del")));
    EXPECT_TRUE(_fileData->saveFileInfoToCache(makeEntity("fdfs_del")));
    EXPECT_TRUE(_fileData->deleteFileInfoByFileIdFromDb(_fileId));
    EXPECT_FALSE(_fileData->getFileInfoByFileIdFromDb(_fileId).has_value());
    _fileData->deleteFileInfoCache(_fileId);
    EXPECT_FALSE(_fileData->getFileInfoFromCacheByFileId(_fileId).has_value());
}

// 5. 用户文件列表：仅返回该用户的文件
TEST_F(FileDataTest, FileListByUser) {
    EXPECT_TRUE(_fileData->saveFileInfoToDb(makeEntity()));
    auto list = _fileData->getFileListByUserIdFromDb("user_ftest");
    ASSERT_FALSE(list.empty());
    bool found = false;
    for (const auto& f : list) {
        if (f.fileId() == _fileId) { found = true; }
        EXPECT_EQ(f.userId(), "user_ftest");   // 列表天然按用户隔离
    }
    EXPECT_TRUE(found);
}

// 6. worksheet 映射：保存 → 查询 → 方案A显式级联删除
TEST_F(FileDataTest, WorksheetMappingLifecycle) {
    // 保存两个映射（模拟一个 Excel 两个 worksheet）
    EXPECT_TRUE(_worksheetData->saveWorksheetMappingToDb(
        WorksheetEntity(_fileId, "Sheet1", "Sheet1_ftest_abc")));
    EXPECT_TRUE(_worksheetData->saveWorksheetMappingToDb(
        WorksheetEntity(_fileId, "成绩表", "成绩表_ftest_abc")));
    // 查询映射
    auto mappings = _worksheetData->getWorksheetMappingsByFileIdFromDb(_fileId);
    ASSERT_EQ(mappings.size(), 2u);
    // 方案A：显式级联删除 → DB 清空
    EXPECT_TRUE(_worksheetData->deleteWorksheetMappingsByFileIdFromDb(_fileId));
    EXPECT_TRUE(_worksheetData->getWorksheetMappingsByFileIdFromDb(_fileId).empty());
}

// 7. worksheet 映射缓存：列表 JSON 往返无损 → 删除后 miss
TEST_F(FileDataTest, WorksheetCacheRoundTrip) {
    std::vector<WorksheetEntity> mappings = {
        WorksheetEntity(_fileId, "Sheet1", "Sheet1_ftest_abc"),
        WorksheetEntity(_fileId, "成绩表", "成绩表_ftest_abc"),
    };
    // 未写入缓存前 miss
    EXPECT_FALSE(_worksheetData->getWorksheetMappingsFromCache(_fileId).has_value());
    // 写入缓存
    EXPECT_TRUE(_worksheetData->saveWorksheetMappingsToCache(_fileId, mappings));
    // 命中且列表无损
    auto cached = _worksheetData->getWorksheetMappingsFromCache(_fileId);
    ASSERT_TRUE(cached.has_value());
    ASSERT_EQ(cached->size(), 2u);
    EXPECT_EQ((*cached)[0].worksheetName(), "Sheet1");
    EXPECT_EQ((*cached)[1].tableName(), "成绩表_ftest_abc");   // 中文表名往返无损
    // 删除缓存后 miss
    _worksheetData->deleteWorksheetMappingsCache(_fileId);
    EXPECT_FALSE(_worksheetData->getWorksheetMappingsFromCache(_fileId).has_value());
}

// G9a：DBBusiness 业务层单元测试（进程内直测，依赖真 MySQL，不依赖其他微服务）
// 覆盖：默认连接契约 / 非法配置 / 表列表与分页 / ExecuteSQL 临时表沙箱语义 /
//       Excel 导入 + 删表 / 非默认连接的生命周期与用户索引
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>
#include <bite_scaffold/log.h>
#include "../../../svc_dbService/dbBusiness.h"
#include "../../../svc_dbService/dbConnMgr.h"
#include "../../../svc_dbService/dbDriver/databaseSchema.h"
#include "../../../svc_dbService/dbDriver/databaseFactory.h"
#include "../../../common/utils.h"
#include "../../../common/errorHandler.h"

using namespace databaseService;

// 测试二进制独立于主程序：dbBusiness.h 只 DECLARE，这里补 DEFINE（与 main.cc 同名同默认值）
DEFINE_string(file_service, "FileService", "文件子服务调用key");

int main(int argc, char** argv) {
    bitelog::bitelog_init();
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

class DBBusinessTest : public ::testing::Test {
protected:
    void SetUp() override {
        MySQLConfig mysqlConf;
        mysqlConf.host = "dev-mysql";
        mysqlConf.port = 3306;
        mysqlConf.username = "root";
        mysqlConf.password = "123456";
        mysqlConf.database = "chat2Data";
        mysqlConf.charset = "utf8mb4";
        _connMgr = std::make_shared<DBConnMgr>();
        // 单测不需要服务发现：给一个空通道管理器即可（SQLite 下载路径才需要 file 服务）
        _svcChannels = std::make_shared<biterpc::SvcChannels>();
        _biz = std::make_shared<DBBusiness>(_connMgr, _svcChannels, mysqlConf);
        _tableName = "tbl_dbtest_" + chat2Data::Utils::generateUuid().substr(0, 8);
    }

    void TearDown() override {
        if (_biz) {
            // 清理测试表（顺带清其临时表）
            try { _biz->dropTableExcel("excel_default", {_tableName}); } catch (...) {}
            // 清默认连接名下的临时表（默认连接本身不可删，会保留）
            try { _biz->disconnectDatabase("excel_default"); } catch (...) {}
        }
    }

    // 构造一份 WorksheetData（2 列 3 行，含中文与浮点）
    chat2Data::excelParseService::WorksheetData makeWorksheetData() {
        chat2Data::excelParseService::WorksheetData ws;
        ws.set_name("Sheet1");
        ws.set_total_rows(4);
        ws.set_total_cols(2);
        auto* col1 = ws.add_columns();
        col1->set_name("客户名称");
        col1->set_type("TEXT");
        auto* col2 = ws.add_columns();
        col2->set_name("金额");
        col2->set_type("DOUBLE");
        const char* names[3] = {"张三", "李四", "王五"};
        double amounts[3] = {1234.56, 2345.67, 3456.78};
        for (int i = 0; i < 3; ++i) {
            auto* row = ws.add_rows();
            auto* c1 = row->add_cells();
            c1->set_value(names[i]);
            c1->set_type("String");
            auto* c2 = row->add_cells();
            c2->set_value(std::to_string(amounts[i]));
            c2->set_type("Float");
        }
        return ws;
    }

    std::shared_ptr<DBConnMgr> _connMgr;
    std::shared_ptr<biterpc::SvcChannels> _svcChannels;
    std::shared_ptr<DBBusiness> _biz;
    std::string _tableName;
};

// 1. 默认连接契约：excel_default 存在、可 ping、不可删
TEST_F(DBBusinessTest, DefaultConnectionContract) {
    auto conn = _connMgr->getConnection("excel_default");
    ASSERT_NE(conn, nullptr);
    EXPECT_TRUE(conn->isDefaultConnection);
    EXPECT_EQ(conn->db->getDatabaseType(), DBType::MYSQL);
    // 请求删除默认连接：返回 true 但连接保留（课件语义，不可删）
    EXPECT_TRUE(_connMgr->removeConnection("excel_default"));
    EXPECT_NE(_connMgr->getConnection("excel_default"), nullptr);
}

// 2. 非法配置被拒（空 host → DB_PARAM_INVALID）
TEST_F(DBBusinessTest, InvalidConfigRejected) {
    chat2Data::DatabaseService::DatabaseConfig config;
    config.set_type(chat2Data::DatabaseService::DATABASE_TYPE_MYSQL);
    config.mutable_mysql_config()->set_host("");          // 缺失主机
    config.mutable_mysql_config()->set_username("root");
    config.mutable_mysql_config()->set_name("chat2Data");
    try {
        _biz->connectDatabase("u1", config);
        FAIL() << "expected Chat2DataException";
    } catch (const chat2Data::Chat2DataException& e) {
        EXPECT_EQ(e.getErrorCode(), chat2Data::ErrorCode::DB_PARAM_INVALID);
    }
    // 未支持的类型同样被拒
    chat2Data::DatabaseService::DatabaseConfig unknown;
    unknown.set_type(chat2Data::DatabaseService::DATABASE_TYPE_UNKNOWN);
    EXPECT_THROW(_biz->connectDatabase("u1", unknown), chat2Data::Chat2DataException);
}

// 3. 表列表 + 分页读取
TEST_F(DBBusinessTest, ListTablesAndPagination) {
    auto tables = _biz->listTables("excel_default");
    bool hasFileInfo = false;
    for (const auto& t : tables) {
        if (t == "tbl_fileInfo") { hasFileInfo = true; }
    }
    EXPECT_TRUE(hasFileInfo);

    auto page = _biz->getTableData("excel_default", "tbl_fileInfo", true, 1, 2);
    EXPECT_EQ(page.pageSize, 2);
    EXPECT_EQ(page.currentPage, 1);
    EXPECT_GE(page.totalRows, 0);
    EXPECT_LE(static_cast<int>(page.rows.size()), 2);
    EXPECT_FALSE(page.columns.empty());
    EXPECT_EQ(page.columns.size(), page.columnTypes.size());
}

// 4. 沙箱语义（核心）：修改走副本，原表零改动，默认读副本，断开清副本
TEST_F(DBBusinessTest, ExecuteSqlSandboxSemantics) {
    const std::string marker = "sandbox_" + chat2Data::Utils::generateUuid().substr(0, 6);

    // 4.1 只读 SQL：query 路径，不建副本
    auto queryResp = _biz->executeSQL("excel_default",
        "SELECT COUNT(*) FROM tbl_fileInfo");
    ASSERT_EQ(queryResp.error_code(), 0);
    EXPECT_TRUE(queryResp.is_query());
    EXPECT_FALSE(queryResp.rows().empty());
    EXPECT_TRUE(_biz->getConnTempTables("excel_default").empty());

    // 4.2 修改 SQL：应被改写到临时表副本执行
    auto modifyResp = _biz->executeSQL("excel_default",
        "UPDATE tbl_fileInfo SET fileName = '" + marker + "'");
    ASSERT_EQ(modifyResp.error_code(), 0);
    EXPECT_FALSE(modifyResp.is_query());

    // 4.3 原表零改动（在副本上改的，原表查不到标记值）
    auto originCheck = _biz->executeSQL("excel_default",
        "SELECT COUNT(*) FROM tbl_fileInfo WHERE fileName = '" + marker + "'");
    ASSERT_EQ(originCheck.error_code(), 0);
    ASSERT_EQ(originCheck.rows_size(), 1);
    EXPECT_EQ(originCheck.rows(0).cells(0), "0");

    // 4.4 副本已登记（名字形如 tbl_fileInfo_temp_<毫秒>）
    auto tempTables = _biz->getConnTempTables("excel_default");
    ASSERT_FALSE(tempTables.empty());
    bool foundTemp = false;
    for (const auto& t : tempTables) {
        if (t.rfind("tbl_fileInfo_temp_", 0) == 0) { foundTemp = true; }
    }
    EXPECT_TRUE(foundTemp);

    // 4.5 默认读副本：看到的是修改后的数据
    auto sandboxView = _biz->getTableData("excel_default", "tbl_fileInfo", false, 1, 5);
    for (const auto& row : sandboxView.rows) {
        ASSERT_GE(row.size(), 2u);
        EXPECT_EQ(row[1], marker);      // fileName 列（id, fileName, ...）
    }
    // 4.6 强制读原表：看不到标记值
    auto originalView = _biz->getTableData("excel_default", "tbl_fileInfo", true, 1, 5);
    for (const auto& row : originalView.rows) {
        ASSERT_GE(row.size(), 2u);
        EXPECT_NE(row[1], marker);
    }

    // 4.7 断开（默认连接会保留，但副本被清理）
    _biz->disconnectDatabase("excel_default");
    EXPECT_TRUE(_biz->getConnTempTables("excel_default").empty());
    EXPECT_NE(_connMgr->getConnection("excel_default"), nullptr);
}

// 5. Excel 数据导入 + 读取 + 删表
TEST_F(DBBusinessTest, ImportExcelDataAndDropTable) {
    auto ws = makeWorksheetData();
    auto importResult = _biz->importExcelData("excel_default", _tableName, ws);
    EXPECT_EQ(importResult.table_name(), _tableName);
    EXPECT_EQ(importResult.imported_rows(), 3);

    // 读回（force_original：导入落到的是原表）
    auto data = _biz->getTableData("excel_default", _tableName, true, 1, 10);
    EXPECT_EQ(data.totalRows, 3);
    ASSERT_EQ(data.rows.size(), 3u);
    ASSERT_EQ(data.columns.size(), 3u);                 // id + 客户名称 + 金额
    // 列名（中文）与值（中文 + 浮点）无损
    EXPECT_EQ(data.columns[1], "客户名称");
    EXPECT_EQ(data.columns[2], "金额");
    EXPECT_EQ(data.rows[0][1], "张三");
    EXPECT_NEAR(std::stod(data.rows[0][2]), 1234.56, 1e-6);

    // 分页：每页 2 行 → 第 2 页 1 行
    auto page2 = _biz->getTableData("excel_default", _tableName, true, 2, 2);
    EXPECT_EQ(page2.totalRows, 3);
    EXPECT_EQ(page2.totalPages, 2);
    EXPECT_EQ(page2.rows.size(), 1u);

    // 删表
    auto dropResult = _biz->dropTableExcel("excel_default", {_tableName});
    EXPECT_EQ(dropResult.dropped_count(), 1);
    auto tablesAfter = _biz->listTables("excel_default");
    bool stillExists = false;
    for (const auto& t : tablesAfter) {
        if (t == _tableName) { stillExists = true; }
    }
    EXPECT_FALSE(stillExists);
}

// 6. 非默认连接生命周期 + 用户索引
TEST_F(DBBusinessTest, UserConnectionLifecycle) {
    chat2Data::DatabaseService::DatabaseConfig config;
    config.set_type(chat2Data::DatabaseService::DATABASE_TYPE_MYSQL);
    auto* mysqlConf = config.mutable_mysql_config();
    mysqlConf->set_host("dev-mysql");
    mysqlConf->set_port(3306);
    mysqlConf->set_username("root");
    mysqlConf->set_password("123456");
    mysqlConf->set_name("chat2Data");
    mysqlConf->set_charset("utf8mb4");

    const std::string userId = "test_user_" + chat2Data::Utils::generateUuid().substr(0, 6);
    std::string connId = _biz->connectDatabase(userId, config);
    EXPECT_FALSE(connId.empty());
    EXPECT_NE(connId, "excel_default");
    // 用户索引里能查到
    auto connIds = _connMgr->getUserConnectionIds(userId);
    ASSERT_EQ(connIds.size(), 1u);
    EXPECT_EQ(connIds[0], connId);
    // 该连接可正常用
    EXPECT_FALSE(_biz->listTables(connId).empty());
    // 断开后从索引与注册表移除
    EXPECT_TRUE(_biz->disconnectDatabase(connId));
    EXPECT_EQ(_connMgr->getConnection(connId), nullptr);
    EXPECT_TRUE(_connMgr->getUserConnectionIds(userId).empty());

    // deleteUserAllConn：一次性清理用户全部连接
    std::string connId2 = _biz->connectDatabase(userId, config);
    ASSERT_FALSE(connId2.empty());
    EXPECT_TRUE(_biz->deleteUserAllConn(userId));
    EXPECT_EQ(_connMgr->getConnection(connId2), nullptr);
}

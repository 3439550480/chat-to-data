// 双驱动样例程序（D8'）：走 DatabaseFactory + IDatabase 抽象接口跑通 MySQL 与 SQLite
// 覆盖：连接/ping → 建表 → 预处理插入（多字符串参数）→ 预处理查询 → 全量查询 →
//       事务回滚 → BLOB(base64 转义) → 表结构 → 表列表 → 标识符转义 → base64 往返
// 说明：MySQL 段使用 chat2Data 库中的临时表 tbl_driver_demo（结束时 DROP 自清理）；
//       SQLite 段使用 /tmp 下的临时 .db 文件（结束时删除）
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <bite_scaffold/log.h>
#include "../../../svc_dbService/dbDriver/databaseFactory.h"
#include "../../../svc_dbService/dbDriver/mysqlDatabase.h"
#include "../../../svc_dbService/dbDriver/sqliteDatabase.h"
#include "../../../common/utils.h"

using namespace databaseService;

static int g_fail = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) {                                                             \
            INF("[OK]   {}", msg);                                              \
        } else {                                                                \
            ERR("[FAIL] {}", msg);                                              \
            ++g_fail;                                                           \
        }                                                                       \
    } while (0)

// ==================== MySQL 段 ====================
static void demoMysql() {
    INF("========== MySQL driver demo ==========");
    MySQLConfig config;
    config.host = "dev-mysql";
    config.port = 3306;
    config.username = "root";
    config.password = "123456";
    config.database = "chat2Data";
    config.charset = "utf8mb4";

    // 走工厂创建实例（校验配置 → 查注册表 → 构造驱动）
    auto db = DatabaseFactory::getInstance().createDatabase(&config);
    CHECK(db != nullptr, "factory create MySQLDatabase");
    if (!db) { return; }

    CHECK(db->getDatabaseType() == DBType::MYSQL, "getDatabaseType == MYSQL");
    CHECK(db->connect(), "connect");
    CHECK(db->ping(), "ping");

    // 建表（自清理的临时表）
    auto createResult = db->executeModify(
        "CREATE TABLE IF NOT EXISTS tbl_driver_demo ("
        " id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        " userName VARCHAR(64) CHARACTER SET utf8mb4,"
        " score DOUBLE, ok BOOLEAN,"
        " blobCol BLOB, remark VARCHAR(128) CHARACTER SET utf8mb4)");
    CHECK(createResult->success(), "CREATE TABLE tbl_driver_demo");

    // 预处理插入：三个字符串参数（覆盖 ㊳ 的 reserve 修复——多个 String 参数并存）
    std::vector<PreparedParam> insertParams = {
        std::string("张三"),           // String
        std::string("这是一个较长的备注内容，用于触发字符串参数的多参数绑定"),  // String
        std::string("BYTES")           // String
    };
    auto insertResult = db->executePreparedModify(
        "INSERT INTO tbl_driver_demo (userName, remark, ok) VALUES (?, ?, 1)", insertParams);
    CHECK(insertResult->success() && insertResult->_affectedRows == 1, "prepared INSERT (3 string params)");

    // 预处理查询：参数化条件（防注入路径）
    std::vector<PreparedParam> queryParams = { std::string("张三") };
    auto preparedQuery = db->executePreparedQuery(
        "SELECT userName, remark FROM tbl_driver_demo WHERE userName = ?", queryParams);
    CHECK(preparedQuery->success() && preparedQuery->rowCount() == 1,
          "prepared SELECT by param");
    if (preparedQuery->rowCount() == 1) {
        CHECK(preparedQuery->getRow(0)[0] == "张三", "prepared SELECT returns 中文无损");
    }

    // BLOB 读路径：写入二进制字面量，读出应为 base64
    db->executeModify("INSERT INTO tbl_driver_demo (userName, blobCol) VALUES ('blob_row', UNHEX('DEADBEEF'))");
    auto blobQuery = db->executeQuery(
        "SELECT blobCol FROM tbl_driver_demo WHERE userName = 'blob_row'");
    CHECK(blobQuery->success() && blobQuery->rowCount() == 1, "SELECT blob column");
    if (blobQuery->rowCount() == 1) {
        CHECK(blobQuery->getRow(0)[0] == "3q2+7w==", "BLOB read as base64 (DEADBEEF -> 3q2+7w==)");
    }

    // 事务回滚：插入后回滚，行数不变
    auto beforeCount = db->executeQuery("SELECT COUNT(*) AS c FROM tbl_driver_demo");
    long long countBefore = std::stoll(beforeCount->getRow(0)[0]);
    CHECK(db->beginTransaction(), "beginTransaction");
    db->executeModify("INSERT INTO tbl_driver_demo (userName) VALUES ('rollback_row')");
    CHECK(db->rollback(), "rollback");
    auto afterCount = db->executeQuery("SELECT COUNT(*) AS c FROM tbl_driver_demo");
    long long countAfter = std::stoll(afterCount->getRow(0)[0]);
    CHECK(countBefore == countAfter, "rollback leaves row count unchanged");

    // 表结构 / 表列表 / 标识符转义
    auto columns = db->getTableStruct("tbl_driver_demo");
    CHECK(columns.size() == 6, "getTableStruct returns 6 columns");
    bool hasAutoInc = false;
    for (const auto& col : columns) {
        if (col.name == "id" && col.autoIncrement) { hasAutoInc = true; }
    }
    CHECK(hasAutoInc, "id column marked autoIncrement");
    auto tables = db->listTables();
    bool found = false;
    for (const auto& t : tables) {
        if (t == "tbl_driver_demo") { found = true; }
    }
    CHECK(found, "listTables contains tbl_driver_demo");
    CHECK(db->quoteIdentifier("tbl_driver_demo") == "`tbl_driver_demo`", "quoteIdentifier uses backticks");

    // 清理
    CHECK(db->executeModify("DROP TABLE IF EXISTS tbl_driver_demo")->success(), "DROP demo table");
    db->disconnect();
}

// ==================== SQLite 段 ====================
static void demoSqlite() {
    INF("========== SQLite driver demo ==========");
    // 准备临时数据库文件（SQLiteConfig::validConfig 要求文件已存在）
    std::string dbPath = "/tmp/dbdemo_" + chat2Data::Utils::generateUuid() + ".db";
    {
        std::ofstream touch(dbPath, std::ios::binary);
        touch.close();
    }

    SQLiteConfig config;
    config.dbPath = dbPath;
    CHECK(config.validConfig(), "SQLiteConfig::validConfig (path + .db + exists)");

    auto db = DatabaseFactory::getInstance().createDatabase(&config);
    CHECK(db != nullptr, "factory create SQLiteDatabase");
    if (!db) { std::remove(dbPath.c_str()); return; }

    CHECK(db->getDatabaseType() == DBType::SQLITE, "getDatabaseType == SQLITE");
    CHECK(db->connect(), "connect");
    CHECK(db->ping(), "ping");

    // 建表（中文表名 + 中文列名，验证 decltype/表结构往返）
    auto createResult = db->executeModify(
        "CREATE TABLE 销售表 (id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " 客户名称 TEXT, 金额 REAL, 已结算 INTEGER, 附件 BLOB)");
    CHECK(createResult->success(), "CREATE TABLE 销售表 (中文标识符)");

    // 预处理插入：中文 + 浮点 + 布尔（SQLite 无 BOOLEAN，绑定为 0/1）
    std::vector<PreparedParam> insertParams = {
        std::string("上海某某贸易有限公司"),
        std::string("备注一"),
        std::string("备注二")
    };
    auto insertResult = db->executePreparedModify(
        "INSERT INTO 销售表 (客户名称, 金额, 已结算) VALUES (?, 1234.56, ?)",
        { insertParams[0], true });
    CHECK(insertResult->success() && insertResult->_affectedRows == 1,
          "prepared INSERT (中文 string param)");

    // 多字符串参数（同一语句两个 String：覆盖绑定稳定性）
    auto insert2 = db->executePreparedModify(
        "INSERT INTO 销售表 (客户名称, 金额) VALUES (?, ?)",
        { std::string("第二个客户"), std::string("") });
    CHECK(insert2->success(), "prepared INSERT with empty-string param (NULL semantics)");

    // 预处理查询
    auto q = db->executePreparedQuery(
        "SELECT 客户名称, 金额 FROM 销售表 WHERE 已结算 = ?", { (long long)1 });
    CHECK(q->success() && q->rowCount() == 1, "prepared SELECT by 已结算=1");
    if (q->rowCount() == 1) {
        CHECK(q->getRow(0)[0] == "上海某某贸易有限公司", "中文值往返无损");
    }

    // BLOB：sqlite 字面量写入二进制，读出应 base64
    db->executeModify("INSERT INTO 销售表 (客户名称, 附件) VALUES ('二进制行', x'DEADBEEF')");
    auto blobQ = db->executeQuery("SELECT 附件 FROM 销售表 WHERE 客户名称 = '二进制行'");
    CHECK(blobQ->success() && blobQ->rowCount() == 1, "SELECT blob column (sqlite)");
    if (blobQ->rowCount() == 1) {
        CHECK(blobQ->getRow(0)[0] == "3q2+7w==", "SQLite BLOB read as base64");
    }

    // 事务回滚
    auto cnt1 = db->executeQuery("SELECT COUNT(*) AS c FROM 销售表");
    long long before = std::stoll(cnt1->getRow(0)[0]);
    CHECK(db->beginTransaction(), "beginTransaction (sqlite)");
    db->executeModify("INSERT INTO 销售表 (客户名称) VALUES ('回滚行')");
    CHECK(db->rollback(), "rollback (sqlite)");
    auto cnt2 = db->executeQuery("SELECT COUNT(*) AS c FROM 销售表");
    CHECK(before == std::stoll(cnt2->getRow(0)[0]), "rollback leaves row count unchanged (sqlite)");

    // 表列表 / 表结构 / 类型转换
    auto tables = db->listTables();
    CHECK(tables.size() == 1 && tables[0] == "销售表", "listTables returns 销售表");
    auto columns = db->getTableStruct("销售表");
    CHECK(columns.size() == 5, "getTableStruct returns 5 columns (PRAGMA table_info)");
    bool pkFound = false;
    for (const auto& col : columns) {
        if (col.name == "id" && col.primaryKey) { pkFound = true; }
    }
    CHECK(pkFound, "id column marked primaryKey");
    CHECK(db->convertExcelTypeToSql("BOOLEAN") == "INTEGER", "convertExcelTypeToSql BOOLEAN->INTEGER");
    CHECK(db->convertExcelTypeToSql("DATE") == "TEXT", "convertExcelTypeToSql DATE->TEXT");
    CHECK(db->convertExcelTypeToSql("BIGINT") == "BIGINT", "convertExcelTypeToSql BIGINT passthrough");
    CHECK(db->quoteIdentifier("销售表") == "\"销售表\"", "quoteIdentifier uses double quotes");

    db->disconnect();
    std::remove(dbPath.c_str());
}

int main() {
    bitelog::bitelog_init();

    // base64 往返校验（驱动 BLOB 转义的基础设施）
    std::vector<char> raw = {'D', 'E', 'A', 'D', '\0', (char)0xBE, (char)0xEF};
    std::string encoded = chat2Data::Utils::base64Encode(raw);
    std::string decoded = chat2Data::Utils::base64Decode(encoded);
    CHECK(decoded.size() == raw.size() && std::equal(decoded.begin(), decoded.end(), raw.begin()),
          "base64 encode/decode roundtrip (with NUL and high bytes)");
    CHECK(chat2Data::Utils::isValidUtf8("中文UTF-8合法") == true, "isValidUtf8 accepts 中文");
    CHECK(chat2Data::Utils::isValidUtf8(std::string("\xC0\x80", 2)) == false,
          "isValidUtf8 rejects overlong encoding");

    demoMysql();
    demoSqlite();

    INF("========== summary: failCount={} ==========", g_fail);
    return g_fail == 0 ? 0 : 1;
}

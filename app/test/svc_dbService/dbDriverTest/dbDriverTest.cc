// D9：dbDriver + SQLValidator 单元测试
// 覆盖：SQL 校验器纯逻辑（㊲ 前缀匹配 / ㊶ 注释对齐 / 三关判定 / 表名提取与校验）
//       + 双驱动集成（工厂 / CRUD / 预处理 / ㊳ 多字符串参数 / ㊴ 与 ㊷ 长文本 / BLOB base64 / 事务）
// 依赖：真 MySQL(dev-mysql) 与真 SQLite（/tmp 临时文件）
#include <gtest/gtest.h>
#include <fstream>
#include <string>
#include <vector>
#include <cstdio>
#include <bite_scaffold/log.h>
#include "../../../svc_dbService/dbDriver/databaseFactory.h"
#include "../../../svc_dbService/dbDriver/mysqlDatabase.h"
#include "../../../svc_dbService/dbDriver/sqliteDatabase.h"
#include "../../../common/utils.h"
#include "../../../common/sqlValidator.h"

using namespace databaseService;

int main(int argc, char** argv) {
    bitelog::bitelog_init();
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

// ==================== SQL 校验器（纯逻辑） ====================

// 1. trim 与规范化
TEST(SQLValidatorTest, TrimAndNormalize) {
    EXPECT_EQ(chat2Data::SQLValidator::trim("   SELECT 1  "), "SELECT 1");
    EXPECT_EQ(chat2Data::SQLValidator::trim("   "), "");
    EXPECT_EQ(chat2Data::SQLValidator::normalize("  -- 注释\n SELECT 1 "), "SELECT 1");
    // 多行注释
    EXPECT_EQ(chat2Data::SQLValidator::normalize("SELECT /* 中间注释 */ 1"), "SELECT  1");
    // 字符串内的注释符必须保留（不误删）
    EXPECT_EQ(chat2Data::SQLValidator::normalize("SELECT '--不是注释'"),
              "SELECT '--不是注释'");
    EXPECT_EQ(chat2Data::SQLValidator::normalize("SELECT '/*也不是*/'"),
              "SELECT '/*也不是*/'");
}

// 2. ㊶ 注释语法对齐 MySQL："--" 后须跟空白才算注释
TEST(SQLValidatorTest, CommentSyntaxAlignedWithMysql) {
    // 无空白 → 不是注释，保留（校验器看到更多内容 → 更保守）
    EXPECT_EQ(chat2Data::SQLValidator::normalize("SELECT 1--2"), "SELECT 1--2");
    // 有空白 → 是注释，删除（换行保留；语句内部空白不额外裁剪）
    EXPECT_EQ(chat2Data::SQLValidator::normalize("SELECT 1 -- 注释\nFROM t"),
              "SELECT 1 \nFROM t");
    // 危险词藏在"非注释"尾部时必须被看到（课件严格删法会漏掉 DROP）
    EXPECT_EQ(chat2Data::SQLValidator::normalize("SELECT 1--2; DROP TABLE t"),
              "SELECT 1--2; DROP TABLE t");
}

// 3. ㊲ 类型判定用首个 token 前缀匹配
TEST(SQLValidatorTest, GetSqlTypePrefixMatching) {
    using chat2Data::SqlType;
    using chat2Data::SQLValidator;
    EXPECT_EQ(SQLValidator::getSQLType("SELECT * FROM t"), SqlType::SELECT);
    EXPECT_EQ(SQLValidator::getSQLType("  select 1"), SqlType::SELECT);
    EXPECT_EQ(SQLValidator::getSQLType("UPDATE t SET a=1"), SqlType::UPDATE);
    EXPECT_EQ(SQLValidator::getSQLType("DELETE FROM t WHERE id=1"), SqlType::DELETE);
    EXPECT_EQ(SQLValidator::getSQLType("INSERT INTO t VALUES(1)"), SqlType::INSERT);
    EXPECT_EQ(SQLValidator::getSQLType("DESC t"), SqlType::DESC);
    EXPECT_EQ(SQLValidator::getSQLType("DESCRIBE t"), SqlType::DESC);
    EXPECT_EQ(SQLValidator::getSQLType("SHOW TABLES"), SqlType::SHOW);
    // 前导注释被剥离后仍能正确判型
    EXPECT_EQ(SQLValidator::getSQLType("/* c */ DELETE FROM t"), SqlType::DELETE);
    // ㊲ 核心回归：语句中间出现的 SELECT 不得改变类型判定
    EXPECT_EQ(SQLValidator::getSQLType("UPDATE t SET remark='SELECT 本月销量'"), SqlType::UPDATE);
    EXPECT_EQ(SQLValidator::getSQLType("INSERT INTO t (note) VALUES('DELETE 掉了')"), SqlType::INSERT);
    // 不在白名单的语句
    EXPECT_EQ(SQLValidator::getSQLType("LOAD DATA INFILE 'x' INTO TABLE t"), SqlType::UNKNOWN);
    EXPECT_EQ(SQLValidator::getSQLType("SET GLOBAL max_connections=1000"), SqlType::UNKNOWN);
    EXPECT_EQ(SQLValidator::getSQLType(""), SqlType::UNKNOWN);
}

// 4. validate 三关（危险词 / 多语句 / 类型可识别）
TEST(SQLValidatorTest, ValidateThreeGates) {
    using chat2Data::SQLValidator;
    // 正常放行
    EXPECT_TRUE(SQLValidator::validate("SELECT * FROM tbl_fileInfo"));
    EXPECT_TRUE(SQLValidator::validate("UPDATE t SET a=1 WHERE id=2"));
    // 关1：危险关键词
    EXPECT_FALSE(SQLValidator::validate("DROP DATABASE chat2Data"));
    EXPECT_FALSE(SQLValidator::validate("TRUNCATE TABLE t"));
    EXPECT_FALSE(SQLValidator::validate("SELECT * FROM t WHERE 1=1"));
    EXPECT_FALSE(SQLValidator::validate("SELECT SLEEP(10)"));
    EXPECT_FALSE(SQLValidator::validate("SELECT * FROM t INTO OUTFILE '/tmp/x'"));
    // 注释夹带绕过（去注释后还原危险词）
    EXPECT_FALSE(SQLValidator::validate("SELECT 1; DR/**/OP DATABASE x"));
    // 关2：多语句
    EXPECT_FALSE(SQLValidator::validate("SELECT 1; DROP TABLE t"));
    EXPECT_FALSE(SQLValidator::validate("SELECT 1; SELECT 2"));
    // 字符串内分号不算多语句
    EXPECT_TRUE(SQLValidator::validate("SELECT * FROM t WHERE name='a;b'"));
    // 尾部悬空分号不算多语句
    EXPECT_TRUE(SQLValidator::validate("SELECT 1;"));
    EXPECT_TRUE(SQLValidator::validate("SELECT 1;;;"));
    // 关3：类型不在白名单
    EXPECT_FALSE(SQLValidator::validate("LOAD DATA INFILE 'x' INTO TABLE t"));
    EXPECT_FALSE(SQLValidator::validate("FLUSH TABLES"));
}

// 5. 只读/修改判定与表名提取
TEST(SQLValidatorTest, ReadOnlyAndTableName) {
    using chat2Data::SQLValidator;
    EXPECT_TRUE(SQLValidator::isReadOnly("SELECT * FROM t"));
    EXPECT_TRUE(SQLValidator::isReadOnly("SHOW TABLES"));
    EXPECT_FALSE(SQLValidator::isReadOnly("UPDATE t SET a=1"));
    EXPECT_TRUE(SQLValidator::isModifySQL("DELETE FROM t"));
    EXPECT_FALSE(SQLValidator::isModifySQL("FLUSH TABLES"));   // 不可识别 → 非修改类
    // 表名提取（含中文与包裹符）
    EXPECT_EQ(SQLValidator::extractTableName("INSERT INTO tbl_fileInfo VALUES(1)"), "tbl_fileInfo");
    EXPECT_EQ(SQLValidator::extractTableName("UPDATE 销售表 SET a=1"), "销售表");
    EXPECT_EQ(SQLValidator::extractTableName("DELETE FROM `tbl_user` WHERE id=1"), "tbl_user");
    EXPECT_EQ(SQLValidator::extractTableName("DROP TABLE tbl_worksheet"), "tbl_worksheet");
    EXPECT_EQ(SQLValidator::extractTableName("CREATE TABLE '订单表' (id INT)"), "订单表");
    // 只读语句不提取表名
    EXPECT_EQ(SQLValidator::extractTableName("SELECT * FROM t"), "");
}

// 6. 表名/列名合法性
TEST(SQLValidatorTest, IdentifierValidation) {
    using chat2Data::SQLValidator;
    EXPECT_TRUE(SQLValidator::isValidTableName("tbl_fileInfo"));
    EXPECT_TRUE(SQLValidator::isValidTableName("销售表"));
    EXPECT_TRUE(SQLValidator::isValidTableName("sheet1_abc"));
    EXPECT_FALSE(SQLValidator::isValidTableName(""));
    EXPECT_FALSE(SQLValidator::isValidTableName("1abc"));            // 数字开头
    EXPECT_FALSE(SQLValidator::isValidTableName("t;drop"));          // 非法字符
    EXPECT_FALSE(SQLValidator::isValidTableName("a..b"));            // 连续点
    EXPECT_FALSE(SQLValidator::isValidTableName("a--b"));            // 连续横线
    EXPECT_FALSE(SQLValidator::isValidTableName(std::string(65, 'a')));  // 超长
    EXPECT_TRUE(SQLValidator::isValidColumnName("user_name"));
    EXPECT_FALSE(SQLValidator::isValidColumnName("user name"));      // 含空格
}

// ==================== MySQL 驱动集成 ====================

class MySQLDriverTest : public ::testing::Test {
protected:
    void SetUp() override {
        MySQLConfig config;
        config.host = "dev-mysql";
        config.port = 3306;
        config.username = "root";
        config.password = "123456";
        config.database = "chat2Data";
        config.charset = "utf8mb4";
        _db = DatabaseFactory::getInstance().createDatabase(&config);
        ASSERT_TRUE(_db != nullptr);
        ASSERT_TRUE(_db->connect());
        _table = "tbl_dbtest_" + chat2Data::Utils::generateUuid().substr(0, 8);
        ASSERT_TRUE(_db->executeModify(
            "CREATE TABLE " + _db->quoteIdentifier(_table) + " ("
            " id BIGINT AUTO_INCREMENT PRIMARY KEY,"
            " userName VARCHAR(64) CHARACTER SET utf8mb4,"
            " remark VARCHAR(4000) CHARACTER SET utf8mb4,"
            " note VARCHAR(64) CHARACTER SET utf8mb4,"
            " score DOUBLE, ok BOOLEAN,"
            " blobCol BLOB)")->success());
    }
    void TearDown() override {
        if (_db) {
            _db->executeModify("DROP TABLE IF EXISTS " + _db->quoteIdentifier(_table));
            _db->disconnect();
        }
    }
    std::shared_ptr<IDatabase> _db;
    std::string _table;
};

// 7. 工厂与基础能力
TEST_F(MySQLDriverTest, FactoryAndBasics) {
    EXPECT_EQ(_db->getDatabaseType(), DBType::MYSQL);
    EXPECT_TRUE(_db->ping());
    EXPECT_EQ(_db->quoteIdentifier("t"), "`t`");
    EXPECT_EQ(_db->quoteIdentifier("`t`"), "`t`");
    EXPECT_EQ(_db->convertExcelTypeToSql("BOOLEAN"), "BOOLEAN");
    auto tables = _db->listTables();
    EXPECT_NE(std::find(tables.begin(), tables.end(), _table), tables.end());
    // 工厂负例
    EXPECT_EQ(DatabaseFactory::getInstance().createDatabase(nullptr), nullptr);
    MySQLConfig bad;
    bad.host = ""; bad.username = "root"; bad.database = "chat2Data";
    EXPECT_EQ(DatabaseFactory::getInstance().createDatabase(&bad), nullptr);   // validConfig 拦截
    EXPECT_TRUE(DatabaseFactory::getInstance().isSupported(DBType::MYSQL));
    EXPECT_TRUE(DatabaseFactory::getInstance().isSupported(DBType::SQLITE));
}

// 8. 预处理插入/查询（㊳ 多字符串参数 + 中文往返）
TEST_F(MySQLDriverTest, PreparedInsertMultiStringParams) {
    // 三个字符串参数同语句：覆盖 _stringBuffer 扩容导致的指针悬空问题
    // （第二个 push_back 触发扩容时，第一个字符串的 c_str() 指针会失效）
    auto ins = _db->executePreparedModify(
        "INSERT INTO " + _db->quoteIdentifier(_table) +
        " (userName, remark, note, ok) VALUES (?, ?, ?, ?)",
        { std::string("张三"), std::string("第二个字符串参数"),
          std::string("第三个字符串参数"), true });
    ASSERT_TRUE(ins->success());
    EXPECT_EQ(ins->_affectedRows, 1);

    auto q = _db->executePreparedQuery(
        "SELECT userName, remark, note FROM " + _db->quoteIdentifier(_table) + " WHERE userName = ?",
        { std::string("张三") });
    ASSERT_TRUE(q->success());
    ASSERT_EQ(q->rowCount(), 1);
    EXPECT_EQ(q->getRow(0)[0], "张三");
    EXPECT_EQ(q->getRow(0)[1], "第二个字符串参数");
    EXPECT_EQ(q->getRow(0)[2], "第三个字符串参数");
}

// 9. ㊷ 长文本（3000 字符）在预处理查询下不被截断
TEST_F(MySQLDriverTest, PreparedQueryLongText) {
    std::string longText;
    for (int i = 0; i < 3000; ++i) { longText += "长"; }   // 3000 个中文 = 9000 字节
    ASSERT_TRUE(_db->executePreparedModify(
        "INSERT INTO " + _db->quoteIdentifier(_table) + " (userName, remark) VALUES (?, ?)",
        { std::string("longrow"), longText })->success());
    auto q = _db->executePreparedQuery(
        "SELECT remark FROM " + _db->quoteIdentifier(_table) + " WHERE userName = ?",
        { std::string("longrow") });
    ASSERT_TRUE(q->success());
    ASSERT_EQ(q->rowCount(), 1);
    EXPECT_EQ(q->getRow(0)[0].size(), longText.size());   // 课件固定 1024 时会在此失败
    EXPECT_EQ(q->getRow(0)[0], longText);
}

// 10. BLOB → base64（㊴ 缓冲区 + 转义路径）
TEST_F(MySQLDriverTest, BlobToBase64) {
    ASSERT_TRUE(_db->executeModify(
        "INSERT INTO " + _db->quoteIdentifier(_table) +
        " (userName, blobCol) VALUES ('blobrow', UNHEX('DEADBEEF'))")->success());
    auto q = _db->executeQuery(
        "SELECT blobCol FROM " + _db->quoteIdentifier(_table) + " WHERE userName='blobrow'");
    ASSERT_TRUE(q->success());
    ASSERT_EQ(q->rowCount(), 1);
    EXPECT_EQ(q->getRow(0)[0], "3q2+7w==");
}

// 11. 事务回滚 + 表结构
TEST_F(MySQLDriverTest, TransactionRollbackAndStruct) {
    auto before = _db->executeQuery("SELECT COUNT(*) FROM " + _db->quoteIdentifier(_table));
    long long countBefore = std::stoll(before->getRow(0)[0]);
    ASSERT_TRUE(_db->beginTransaction());
    ASSERT_TRUE(_db->executeModify(
        "INSERT INTO " + _db->quoteIdentifier(_table) + " (userName) VALUES ('rollback')")->success());
    ASSERT_TRUE(_db->rollback());
    auto after = _db->executeQuery("SELECT COUNT(*) FROM " + _db->quoteIdentifier(_table));
    EXPECT_EQ(std::stoll(after->getRow(0)[0]), countBefore);
    // 表结构：7 列，id 自增主键
    auto columns = _db->getTableStruct(_table);
    EXPECT_EQ(columns.size(), 7u);
    for (const auto& col : columns) {
        if (col.name == "id") {
            EXPECT_TRUE(col.primaryKey);
            EXPECT_TRUE(col.autoIncrement);
        }
        if (col.name == "remark") {
            EXPECT_FALSE(col.type.empty());
        }
    }
}

// ==================== SQLite 驱动集成 ====================

class SQLiteDriverTest : public ::testing::Test {
protected:
    void SetUp() override {
        _dbPath = "/tmp/dbtest_" + chat2Data::Utils::generateUuid() + ".db";
        std::ofstream touch(_dbPath, std::ios::binary);
        touch.close();
        SQLiteConfig config;
        config.dbPath = _dbPath;
        ASSERT_TRUE(config.validConfig());
        _db = DatabaseFactory::getInstance().createDatabase(&config);
        ASSERT_TRUE(_db != nullptr);
        ASSERT_TRUE(_db->connect());
        ASSERT_TRUE(_db->executeModify(
            "CREATE TABLE 销售表 (id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " 客户名称 TEXT, 金额 REAL, 已结算 INTEGER, 附件 BLOB)")->success());
    }
    void TearDown() override {
        if (_db) { _db->disconnect(); }
        std::remove(_dbPath.c_str());
    }
    std::shared_ptr<IDatabase> _db;
    std::string _dbPath;
};

// 12. SQLite：中文标识符 + 预处理 + 事务 + 元数据 + 类型转换
TEST_F(SQLiteDriverTest, CrudPreparedAndMeta) {
    EXPECT_EQ(_db->getDatabaseType(), DBType::SQLITE);
    EXPECT_TRUE(_db->ping());
    // 预处理插入（中文参数）
    ASSERT_TRUE(_db->executePreparedModify(
        "INSERT INTO 销售表 (客户名称, 金额, 已结算) VALUES (?, ?, ?)",
        { std::string("上海某某贸易有限公司"), 1234.56, true })->success());
    // 多字符串参数（一个非空 + 一个空串走 NULL 语义）
    ASSERT_TRUE(_db->executePreparedModify(
        "INSERT INTO 销售表 (客户名称, 金额) VALUES (?, ?)",
        { std::string("第二客户"), std::string("") })->success());
    // 预处理查询
    auto q = _db->executePreparedQuery("SELECT 客户名称 FROM 销售表 WHERE 已结算 = ?",
                                       { (long long)1 });
    ASSERT_TRUE(q->success());
    ASSERT_EQ(q->rowCount(), 1);
    EXPECT_EQ(q->getRow(0)[0], "上海某某贸易有限公司");
    // 事务回滚
    auto cnt1 = _db->executeQuery("SELECT COUNT(*) FROM 销售表");
    long long before = std::stoll(cnt1->getRow(0)[0]);
    ASSERT_TRUE(_db->beginTransaction());
    ASSERT_TRUE(_db->executeModify("INSERT INTO 销售表 (客户名称) VALUES ('回滚行')")->success());
    ASSERT_TRUE(_db->rollback());
    auto cnt2 = _db->executeQuery("SELECT COUNT(*) FROM 销售表");
    EXPECT_EQ(std::stoll(cnt2->getRow(0)[0]), before);
    // 元数据
    auto tables = _db->listTables();
    ASSERT_EQ(tables.size(), 1u);
    EXPECT_EQ(tables[0], "销售表");
    auto columns = _db->getTableStruct("销售表");
    EXPECT_EQ(columns.size(), 5u);
    for (const auto& col : columns) {
        if (col.name == "id") { EXPECT_TRUE(col.primaryKey); }
    }
    EXPECT_EQ(_db->quoteIdentifier("销售表"), "\"销售表\"");
    // 类型转换：SQLite 无 BOOLEAN / DATE
    EXPECT_EQ(_db->convertExcelTypeToSql("BOOLEAN"), "INTEGER");
    EXPECT_EQ(_db->convertExcelTypeToSql("DATE"), "TEXT");
    EXPECT_EQ(_db->convertExcelTypeToSql("DOUBLE"), "DOUBLE");
}

// 13. SQLite：BLOB → base64
TEST_F(SQLiteDriverTest, BlobToBase64) {
    ASSERT_TRUE(_db->executeModify(
        "INSERT INTO 销售表 (客户名称, 附件) VALUES ('二进制行', x'DEADBEEF')")->success());
    auto q = _db->executeQuery("SELECT 附件 FROM 销售表 WHERE 客户名称='二进制行'");
    ASSERT_TRUE(q->success());
    ASSERT_EQ(q->rowCount(), 1);
    EXPECT_EQ(q->getRow(0)[0], "3q2+7w==");
}

// 14. SQLite：配置文件不存在 → 配置无效 → 工厂拒绝创建
TEST_F(SQLiteDriverTest, InvalidConfigRejected) {
    SQLiteConfig config;
    config.dbPath = "/tmp/not_exists_" + chat2Data::Utils::generateUuid() + ".db";
    EXPECT_FALSE(config.validConfig());
    EXPECT_EQ(DatabaseFactory::getInstance().createDatabase(&config), nullptr);
    // 非 .db 后缀
    SQLiteConfig config2;
    config2.dbPath = _dbPath + ".bak";
    EXPECT_FALSE(config2.validConfig());
}

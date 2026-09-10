#pragma once
#include <sqlite3.h>
#include <memory>
#include <vector>
#include <string>
#include "database.h"
#include "databaseSchema.h"

namespace databaseService {

class SQLiteDatabase : public IDatabase {
public:
    explicit SQLiteDatabase(const SQLiteConfig& config);
    ~SQLiteDatabase() override;
    bool connect() override;
    void disconnect() override;
    bool ping() override;
    std::shared_ptr<QueryResult> executeQuery(const std::string& sql) override;
    std::shared_ptr<QueryResult> executeModify(const std::string& sql) override;
    std::shared_ptr<QueryResult> executePreparedQuery(const std::string& sql,
                                                      const std::vector<PreparedParam>& params) override;
    std::shared_ptr<QueryResult> executePreparedModify(const std::string& sql,
                                                       const std::vector<PreparedParam>& params) override;
    bool beginTransaction() override;
    bool commit() override;
    bool rollback() override;
    std::string quoteIdentifier(const std::string& identifier) override;
    std::vector<std::string> listTables() override;
    std::vector<ColumnInfo> getTableStruct(const std::string& tableName) override;
    std::string convertExcelTypeToSql(const std::string& excelType) override;
    DBType getDatabaseType() const override;
private:
    // 初始化数据库连接（打开文件句柄）
    bool initDatabase();
    // 关闭数据库连接
    void closeDatabase();
    // 参数绑定器辅助类（prepare+bind+step 一体化，与 MySQL 版两段式不同）
    class ParamBinder;
private:
    SQLiteConfig _config;
    sqlite3* _db = nullptr;
    bool _connected = false;
};

// 参数绑定器辅助类：RAII 持有预编译语句（析构自动 finalize）
class SQLiteDatabase::ParamBinder {
public:
    ParamBinder(sqlite3* db, const std::string& sql, const std::vector<PreparedParam>& params);
    ~ParamBinder();
    // 获取预编译语句句柄（nullptr 表示 prepare 失败）
    sqlite3_stmt* getStmt() const;
    // 执行语句，返回 sqlite3_step 的返回值（SQLITE_ROW/DONE/ERROR）
    int step();
private:
    sqlite3_stmt* _stmt = nullptr;    // 预编译语句句柄
    bool _executed = false;           // 是否已执行
    int _columnCount = 0;             // 查询结果列数
};

} // namespace databaseService

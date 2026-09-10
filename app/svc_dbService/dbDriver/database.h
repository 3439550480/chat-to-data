#pragma once
#include <string>
#include <memory>
#include <vector>
#include "databaseSchema.h"

namespace databaseService {

// 数据库抽象接口：MySQL/SQLite 驱动的统一契约（业务层只依赖本接口）
// 职责分组：连接管理 / CURD（含预处理）/ 事务 / 表结构与类型 / 防注入转义
class IDatabase {
public:
    virtual ~IDatabase() = default;
    // 连接数据库
    virtual bool connect() = 0;
    // 断开数据库连接
    virtual void disconnect() = 0;
    // 检查数据库连接是否有效
    virtual bool ping() = 0;
    // 执行查询语句
    virtual std::shared_ptr<QueryResult> executeQuery(const std::string& sql) = 0;
    // 执行修改语句
    virtual std::shared_ptr<QueryResult> executeModify(const std::string& sql) = 0;
    // 执行预处理查询语句（参数化，防注入）
    virtual std::shared_ptr<QueryResult> executePreparedQuery(const std::string& sql,
                                                              const std::vector<PreparedParam>& params) = 0;
    // 执行预处理修改语句（参数化，防注入）
    virtual std::shared_ptr<QueryResult> executePreparedModify(const std::string& sql,
                                                               const std::vector<PreparedParam>& params) = 0;
    // 开始事务
    virtual bool beginTransaction() = 0;
    // 提交事务
    virtual bool commit() = 0;
    // 回滚事务
    virtual bool rollback() = 0;
    // 转义字段名（MySQL 用反引号、SQLite 用双引号——防关键字冲突）
    virtual std::string quoteIdentifier(const std::string& identifier) = 0;
    // 获取数据库表列表
    virtual std::vector<std::string> listTables() = 0;
    // 获取指定表的列信息
    virtual std::vector<ColumnInfo> getTableStruct(const std::string& tableName) = 0;
    // 将Excel解析服务输出的类型（BIGINT/DOUBLE/BOOLEAN/DATE/TEXT）转换为当前数据库支持的SQL类型
    virtual std::string convertExcelTypeToSql(const std::string& excelType) = 0;
    // 获取数据库类型
    virtual DBType getDatabaseType() const = 0;
};

} // namespace databaseService

#pragma once
#include <mysql/mysql.h>
#include <memory>
#include <vector>
#include <string>
#include "database.h"
#include "databaseSchema.h"

namespace databaseService {

class MySQLDatabase : public IDatabase {
public:
    explicit MySQLDatabase(const MySQLConfig& config);
    ~MySQLDatabase() override;
    // 连接数据库
    bool connect() override;
    // 断开数据库连接
    void disconnect() override;
    // 检查数据库连接是否有效
    bool ping() override;
    // 执行查询语句
    std::shared_ptr<QueryResult> executeQuery(const std::string& sql) override;
    // 执行修改语句
    std::shared_ptr<QueryResult> executeModify(const std::string& sql) override;
    // 执行预处理查询语句
    std::shared_ptr<QueryResult> executePreparedQuery(const std::string& sql,
                                                      const std::vector<PreparedParam>& params) override;
    // 执行预处理修改语句
    std::shared_ptr<QueryResult> executePreparedModify(const std::string& sql,
                                                       const std::vector<PreparedParam>& params) override;
    // 开始事务
    bool beginTransaction() override;
    // 提交事务
    bool commit() override;
    // 回滚事务
    bool rollback() override;
    // 转义字段名（反引号包裹）
    std::string quoteIdentifier(const std::string& identifier) override;
    // 获取数据库表列表
    std::vector<std::string> listTables() override;
    // 获取表结构（DESC 表）
    std::vector<ColumnInfo> getTableStruct(const std::string& tableName) override;
    // Excel类型 → MySQL类型（BIGINT/DOUBLE/BOOLEAN/DATE/TEXT 直接透传）
    std::string convertExcelTypeToSql(const std::string& excelType) override;
    // 获取数据库类型
    DBType getDatabaseType() const override;
private:
    // 参数绑定器辅助类
    class ParamBinder;
    // 结果绑定器辅助类
    class ResultBinder;
    // 初始化MySQL连接
    bool initMysql();
    // 关闭MySQL连接
    void closeMysql();
    // 将MySQL字段类型转换为字符串
    std::string convertColumnType(enum_field_types type, unsigned int charsetnr);
private:
    MySQLConfig _config;              // MySQL数据库配置
    MYSQL* _mysql = nullptr;          // MySQL连接句柄
    bool _connected = false;          // 是否已连接
};

// ==================== 参数绑定器（㊳ 修复所在） ====================
class MySQLDatabase::ParamBinder {
public:
    ParamBinder(const std::string& sql, const std::vector<PreparedParam>& params);
    ~ParamBinder();
    // 获取参数绑定结果
    MYSQL_BIND* getBinds();
    // 获取参数数量
    size_t getParamCount() const;
private:
    // 按照索引位置绑定参数
    void bindParam(size_t index, const PreparedParam& param);
private:
    std::vector<MYSQL_BIND> _binds;                 // 保存参数绑定结果
    std::vector<long long> _longBuffer;             // 保存整数参数
    std::vector<double> _doubleBuffer;              // 保存浮点数参数
    std::vector<std::string> _stringBuffer;         // 保存字符串参数（㊳：reserve 防扩容悬空）
    std::vector<unsigned long> _stringLengthBuffer; // 保存字符串参数长度
    bool* _boolBuffer;                              // 保存布尔参数
    bool* _nullBuffer;                              // 保存空值参数
    size_t _paramCount;
};

// ==================== 结果绑定器（㊴ 修复所在） ====================
class MySQLDatabase::ResultBinder {
public:
    ResultBinder(MYSQL_STMT* stmt, MYSQL_RES* meta, std::shared_ptr<QueryResult> result);
    ~ResultBinder();
    bool bind();
private:
    std::vector<MYSQL_BIND> _binds;                 // 保存结果绑定
    std::vector<std::string> _stringBuffer;         // 保存字符串结果（㊴：预扩 1024 字节）
    std::vector<unsigned long> _stringLengthBuffer; // 实际长度由 mysql_stmt_fetch 回填
    std::vector<long long> _longBuffer;
    std::vector<double> _doubleBuffer;
    bool* _boolBuffer;
    bool* _nullBuffer;
    size_t _fieldCount;
    MYSQL_STMT* _stmt = nullptr;
    MYSQL_RES* _meta = nullptr;
    std::shared_ptr<QueryResult> _result;
};

} // namespace databaseService

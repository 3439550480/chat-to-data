#pragma once
#include <string>
#include <vector>
#include <regex>

namespace chat2Data {

// 支持的SQL类型
enum class SqlType {
    SELECT,
    SHOW,
    DESC,
    INSERT,
    UPDATE,
    DELETE,
    REPLACE,
    TRUNCATE,
    CREATE,
    DROP,
    ALTER,
    UNKNOWN
};

// SQL 校验器：规范化 + 安全检测 + 类型判定（数据库子服务与 AI 子服务共用）
class SQLValidator {
public:
    // 去除SQL语句首尾空白字符
    static std::string trim(const std::string& sql);
    // 去除SQL语句中的注释（状态机：不误伤字符串内的注释符）
    // 课件问题㊶修订：单行注释对齐 MySQL 语法（"--" 后须跟空白或行尾才算注释），
    // 避免把 MySQL 的真代码（如 "SELECT 1--2"）当注释删掉，导致校验器"看得比执行少"
    static std::string removeComments(const std::string& sql);
    // 规范SQL语句（去注释 + 去首尾空白）
    static std::string normalize(const std::string& sql);
    // 验证SQL语句是否有效（危险关键词 + 多语句 + 类型可识别，三关全过才算有效）
    static bool validate(const std::string& sql);
    // 获取SQL语句的类型（课件问题㊲修复：前缀token精确匹配，非全文find）
    static SqlType getSQLType(const std::string& sql);
    // 判断SQL语句是否为只读操作
    static bool isReadOnly(const std::string& sql);
    // 判断SQL语句是否为修改操作
    static bool isModifySQL(const std::string& sql);
    // 检测SQL语句是否包含危险关键词
    static bool containsDangerousKeywords(const std::string& sql);
    // 检测SQL语句是否包含多个语句（分号且其后仍有内容）
    static bool hasMultipleStatements(const std::string& sql);
    // 从修改类SQL语句中提取表名
    static std::string extractTableName(const std::string& sql);
    // 判断表名是否有效（支持中文表名）
    static bool isValidTableName(const std::string& tableName);
    // 判断列名是否有效
    static bool isValidColumnName(const std::string& columnName);
private:
    static const std::vector<std::string> DANGEROUS_KEYWORDS;
    static const std::regex COMMENT_SINGLE_LINE;
    static const std::regex COMMENT_MULTI_LINE;
    static const std::regex PATTERN_TABLE_NAME;
};

} // namespace chat2Data

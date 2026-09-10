#include "sqlValidator.h"
#include <algorithm>
#include <cctype>
#include <bite_scaffold/log.h>

namespace chat2Data {

// 危险关键词表（大写比对）
const std::vector<std::string> SQLValidator::DANGEROUS_KEYWORDS = {
    "DROP DATABASE",
    "TRUNCATE",
    "EXEC",
    "EXECUTE",
    "SCRIPT",
    "JAVASCRIPT",
    "EVAL",
    "UNION ALL SELECT",
    "1=1", "OR 1=1",
    "' OR '1'='1'",
    "SLEEP(",
    "BENCHMARK(",
    "LOAD_FILE(",
    "INTO OUTFILE",
    "INTO DUMPFILE"
};

// R"(--[^\r\n]*)" 匹配单行注释（本类保留正则成员供扩展，实际去注释走状态机）
const std::regex SQLValidator::COMMENT_SINGLE_LINE(R"(--[^\r\n]*)");
// R"(/\*[\s\S]*?\*/)" 匹配多行注释
const std::regex SQLValidator::COMMENT_MULTI_LINE(R"(/\*[\s\S]*?\*/)");
// R"(^([a-zA-Z_][a-zA-Z0-9_\.\-]*|[^\s]+)$)" 表名模式（保留供扩展）
const std::regex SQLValidator::PATTERN_TABLE_NAME(R"(^([a-zA-Z_][a-zA-Z0-9_\.\-]*|[^\s]+)$)");

// 去除SQL语句首尾空白字符
std::string SQLValidator::trim(const std::string& sql) {
    // 1. 从前往后找第一个非空白字符的位置---正向迭代器
    auto start = std::find_if_not(sql.begin(), sql.end(), [](unsigned char ch) {
        return std::isspace(ch);
    });
    // 2. 从后往前找第一个非空白字符的位置（反向迭代器 base() 转正向）
    auto end = std::find_if_not(sql.rbegin(), sql.rend(), [](unsigned char ch) {
        return std::isspace(ch);
    }).base();
    if (start >= end) {
        return "";
    }
    // 3. 用 [begin, end) 之间的非空白字符重建字符串
    return std::string(start, end);
}

// 去除SQL语句中的注释
// 实现原理：状态机逐字符扫描，按当前字符与状态决定处理方式
// 状态：普通字符 / 单行注释 / 多行注释 / 单引号串 / 双引号串 / 转义字符
// 处理流程：
// 1. 单行注释"--"：跳过后续所有字符直到换行或结束
// 2. 多行注释"/* */"：跳过直到闭合
// 3. 字符串（单引号或双引号）：整体保留，处理转义字符与 '' 双写
// 4. 普通字符：直接保留到结果中
std::string SQLValidator::removeComments(const std::string& sql) {
    std::string result;
    result.reserve(sql.length());
    size_t i = 0;
    while (i < sql.length()) {
        // 1. 检测单行注释 "--"
        // 课件问题㊶修订：对齐 MySQL 语法——"--" 后必须紧跟空白或行尾才算注释。
        // 原因：MySQL 中 "SELECT 1--2" 的 "--" 是运算符而非注释；若按课件"见--即删"，
        // 校验器会把真代码整段删掉（如 "SELECT 1--2; DROP TABLE t" 被删成 "SELECT 1"），
        // 导致危险词/多语句检查漏检。对齐后删得更少 → 校验看得更多 → 失败方向更保守
        if (i + 1 < sql.length() && sql[i] == '-' && sql[i + 1] == '-' &&
            (i + 2 >= sql.length() ||
             std::isspace(static_cast<unsigned char>(sql[i + 2])))) {
            // 跳过单行注释直到行尾或字符串结束（换行符本身由外层循环保留）
            while (i < sql.length() && sql[i] != '\n' && sql[i] != '\r') {
                ++i;
            }
            continue;
        }
        // 2. 检测多行注释 "/* */"
        if (i + 1 < sql.length() && sql[i] == '/' && sql[i + 1] == '*') {
            // 跳过 "/*" 两个字符
            i += 2;
            // 跳过直到 "*/"（含）
            while (i + 1 < sql.length()) {
                if (sql[i] == '*' && sql[i + 1] == '/') {
                    i += 2;
                    break;
                }
                ++i;
            }
            continue;
        }
        // 3. 检测字符串开始：单引号或双引号（串内注释符不生效，整体保留）
        if (sql[i] == '\'' || sql[i] == '"') {
            char quoteChar = sql[i];
            result += sql[i];       // 保留左引号
            ++i;
            // 跳过字符串内容，处理转义字符
            while (i < sql.length()) {
                if (sql[i] == '\\' && i + 1 < sql.length()) {
                    // 转义字符：反斜杠与后一个字符原样保留
                    result += sql[i];
                    ++i;
                    if (i < sql.length()) {
                        result += sql[i];
                        ++i;
                    }
                } else if (sql[i] == quoteChar) {
                    // 检查是否是转义引号（两个连续引号表示一个引号）
                    if (i + 1 < sql.length() && sql[i + 1] == quoteChar) {
                        result += sql[i];
                        result += sql[i + 1];
                        i += 2;
                    } else {
                        // 字符串结束
                        result += sql[i];
                        ++i;
                        break;
                    }
                } else {
                    result += sql[i];
                    ++i;
                }
            }
            continue;
        }
        // 4. 普通字符：原样保留
        result += sql[i];
        ++i;
    }
    return result;
}

// 规范SQL语句（去注释 + 去首尾空白）
std::string SQLValidator::normalize(const std::string& sql) {
    std::string result = removeComments(sql);
    result = trim(result);
    return result;
}

// 验证SQL语句是否有效（三关：危险关键词 → 多语句 → 类型可识别）
bool SQLValidator::validate(const std::string& sql) {
    // 1. 检查是否包含危险关键词
    if (containsDangerousKeywords(sql)) {
        return false;
    }
    // 2. 检查是否包含多个语句
    if (hasMultipleStatements(sql)) {
        return false;
    }
    // 3. 检测是否为支持的SQL类型
    return getSQLType(sql) != SqlType::UNKNOWN;
}

// 获取SQL语句的类型
// 课件问题㊲修复：课件用 find 全文匹配且 SELECT 优先——"UPDATE t SET remark='SELECT...'"
// 会被误判为 SELECT（只读），使修改语句走查询通道，风险不对称。
// 修复：取规范化后的【首个单词】精确比对（跳过前导空白，遇非字母终止）——
// 只有语句开头的关键字才决定类型；顺删课件中重复的 DELETE 分支死代码
SqlType SQLValidator::getSQLType(const std::string& sql) {
    // 1. 规范化（去注释+去首尾空白）
    std::string normalizedSql = normalize(sql);
    // 2. 转大写
    std::transform(normalizedSql.begin(), normalizedSql.end(), normalizedSql.begin(), ::toupper);
    // 3. 取首个 token：跳过前导空白，收集连续字母
    size_t i = 0;
    while (i < normalizedSql.size() &&
           std::isspace(static_cast<unsigned char>(normalizedSql[i]))) {
        ++i;
    }
    std::string token;
    while (i < normalizedSql.size() &&
           std::isalpha(static_cast<unsigned char>(normalizedSql[i]))) {
        token += normalizedSql[i];
        ++i;
    }
    if (token.empty()) {
        return SqlType::UNKNOWN;
    }
    // 4. 首 token 精确匹配（语句中间出现的关键词不再干扰判定）
    if (token == "SELECT")   { return SqlType::SELECT; }
    if (token == "SHOW")     { return SqlType::SHOW; }
    if (token == "DESC" || token == "DESCRIBE") { return SqlType::DESC; }
    if (token == "INSERT")   { return SqlType::INSERT; }
    if (token == "UPDATE")   { return SqlType::UPDATE; }
    if (token == "DELETE")   { return SqlType::DELETE; }
    if (token == "REPLACE")  { return SqlType::REPLACE; }
    if (token == "TRUNCATE") { return SqlType::TRUNCATE; }
    if (token == "CREATE")   { return SqlType::CREATE; }
    if (token == "DROP")     { return SqlType::DROP; }
    if (token == "ALTER")    { return SqlType::ALTER; }
    // 5. 其他类型，即不支持的SQL语句
    return SqlType::UNKNOWN;
}

// 判断SQL语句是否为只读操作
bool SQLValidator::isReadOnly(const std::string& sql) {
    // 1. 获取SQL语句的类型
    SqlType type = getSQLType(sql);
    // 2. 检查是否为只读操作
    return type == SqlType::SELECT || type == SqlType::SHOW || type == SqlType::DESC;
}

// 判断SQL语句是否为修改操作
bool SQLValidator::isModifySQL(const std::string& sql) {
    // 1. 获取SQL语句的类型
    SqlType type = getSQLType(sql);
    // 2. 不支持的类型视为非修改类
    if (type == SqlType::UNKNOWN) {
        return false;
    }
    // 3. 非只读即修改类
    return !isReadOnly(sql);
}

// 检测SQL语句是否包含危险关键词
// 先规范化（去注释可拦 "DR/**/OP" 型夹带绕过）再转大写比对
bool SQLValidator::containsDangerousKeywords(const std::string& sql) {
    // 1. 规范SQL语句
    std::string normalizedSql = normalize(sql);
    // 2. 转大写
    std::transform(normalizedSql.begin(), normalizedSql.end(), normalizedSql.begin(), ::toupper);
    // 3. 逐个比对危险关键词
    for (const auto& keyword : DANGEROUS_KEYWORDS) {
        if (normalizedSql.find(keyword) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// 检测SQL中是否包含多条SQL语句
// 实现原理：遍历SQL字符串，找到字符串外的第一个分号；若其后还有非空白内容即多语句
// 处理流程：
// 1. 遍历每个字符；遇到引号进入字符串模式，整体跳过（含转义与 '' 双写）
// 2. 字符串模式外的分号：检查其后内容（trim 后为空 → 单语句；
//    若其后紧跟更多分号则跳过继续；否则判定为多语句）
bool SQLValidator::hasMultipleStatements(const std::string& sql) {
    std::string normalizedSql = normalize(sql);
    if (normalizedSql.empty()) {
        return false;
    }
    size_t i = 0;
    while (i < normalizedSql.length()) {
        if (normalizedSql[i] == '\'' || normalizedSql[i] == '"') {
            // 字符串开始，跳过字符串内容（内部的分号不算语句分隔）
            char quoteChar = normalizedSql[i];
            ++i;
            while (i < normalizedSql.length()) {
                if (normalizedSql[i] == '\\' && i + 1 < normalizedSql.length()) {
                    // 转义字符，跳过两个字符
                    i += 2;
                } else if (normalizedSql[i] == quoteChar) {
                    // 检查是否是转义的引号（'' 双写）
                    if (i + 1 < normalizedSql.length() && normalizedSql[i + 1] == quoteChar) {
                        i += 2;
                    } else {
                        // 字符串结束
                        ++i;
                        break;
                    }
                } else {
                    ++i;
                }
            }
        } else if (normalizedSql[i] == ';') {
            // 找到字符串外的分号：看其后是否还有内容
            std::string afterSemicolon = normalizedSql.substr(i + 1);
            afterSemicolon = trim(afterSemicolon);
            if (afterSemicolon.empty()) {
                return false;    // 分号后无内容 = 单语句（允许尾部悬空分号）
            }
            if (afterSemicolon[0] == ';') {
                // 连续分号：继续向后跳过（如 "SELECT 1;;;"）
                while (i < normalizedSql.length() &&
                       (normalizedSql[i] == ';' ||
                        std::isspace(static_cast<unsigned char>(normalizedSql[i])))) {
                    ++i;
                }
                continue;
            }
            return true;         // 分号后仍有内容 = 多语句
        } else {
            ++i;
        }
    }
    return false;
}

// 从SQL语句中提取表名
// 只提取修改类SQL（INSERT/UPDATE/DELETE/TRUNCATE/REPLACE/ALTER/CREATE/DROP）的表名；
// 不用正则，支持中文表名与单引号/反引号包裹
// 定位规则：INSERT INTO / UPDATE / DELETE FROM / TRUNCATE TABLE / REPLACE INTO /
//           ALTER TABLE / CREATE TABLE / DROP TABLE
std::string SQLValidator::extractTableName(const std::string& sql) {
    std::string table;
    std::string normalizedSql = normalize(sql);
    if (normalizedSql.empty()) { return table; }
    // 1. 判断是否为修改类SQL语句（只读语句直接返回空）
    SqlType type = getSQLType(normalizedSql);
    if (type != SqlType::INSERT && type != SqlType::UPDATE && type != SqlType::DELETE &&
        type != SqlType::TRUNCATE && type != SqlType::REPLACE && type != SqlType::ALTER &&
        type != SqlType::CREATE && type != SqlType::DROP) {
        return table;
    }
    // 2. 转大写用于关键字定位（表名本身仍从原串提取，保住中文大小写）
    std::string upperSql;
    upperSql.reserve(normalizedSql.length());
    for (char c : normalizedSql) {
        upperSql += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    // 3. 按语句类型定位表名起始位置
    size_t tableNameStart = std::string::npos;
    if (upperSql.find("INSERT") != std::string::npos) {          // INSERT INTO table_name
        size_t intoPos = upperSql.find("INTO");
        if (intoPos != std::string::npos) {
            tableNameStart = intoPos + 4;
        }
    } else if (upperSql.find("UPDATE") != std::string::npos) {   // UPDATE table_name
        size_t updatePos = upperSql.find("UPDATE");
        if (updatePos != std::string::npos) {
            tableNameStart = updatePos + 6;
        }
    } else if (upperSql.find("DELETE") != std::string::npos) {   // DELETE FROM table_name
        size_t fromPos = upperSql.find("FROM");
        if (fromPos != std::string::npos) {
            tableNameStart = fromPos + 4;
        }
    } else if (upperSql.find("TRUNCATE") != std::string::npos) { // TRUNCATE TABLE table_name
        size_t tablePos = upperSql.find("TABLE");
        if (tablePos != std::string::npos) {
            tableNameStart = tablePos + 5;
        }
    } else if (upperSql.find("REPLACE") != std::string::npos) {  // REPLACE INTO table_name
        size_t intoPos = upperSql.find("INTO");
        if (intoPos != std::string::npos) {
            tableNameStart = intoPos + 4;
        }
    } else if (upperSql.find("ALTER") != std::string::npos) {    // ALTER TABLE table_name
        size_t tablePos = upperSql.find("TABLE");
        if (tablePos != std::string::npos) {
            tableNameStart = tablePos + 5;
        }
    } else if (upperSql.find("CREATE") != std::string::npos) {   // CREATE TABLE table_name
        size_t tablePos = upperSql.find("TABLE");
        if (tablePos != std::string::npos) {
            tableNameStart = tablePos + 5;
        }
    } else if (upperSql.find("DROP") != std::string::npos) {     // DROP TABLE table_name
        size_t tablePos = upperSql.find("TABLE");
        if (tablePos != std::string::npos) {
            tableNameStart = tablePos + 5;
        }
    }
    if (tableNameStart == std::string::npos) {
        return table;
    }
    // 4. 跳过空白定位表名
    while (tableNameStart < normalizedSql.length() &&
           std::isspace(static_cast<unsigned char>(normalizedSql[tableNameStart]))) {
        ++tableNameStart;
    }
    if (tableNameStart >= normalizedSql.length()) {
        return table;
    }
    // 5. 提取表名（支持单引号/反引号包裹，处理 '' 双写）
    std::string tableName;
    if (normalizedSql[tableNameStart] == '\'' || normalizedSql[tableNameStart] == '`') {
        char quoteChar = normalizedSql[tableNameStart];
        ++tableNameStart;
        size_t endQuotePos = tableNameStart;
        while (endQuotePos < normalizedSql.length()) {
            if (normalizedSql[endQuotePos] == quoteChar) {
                if (quoteChar == '\'' && endQuotePos + 1 < normalizedSql.length() &&
                    normalizedSql[endQuotePos + 1] == '\'') {
                    endQuotePos += 2;    // 转义的单引号
                } else {
                    break;
                }
            } else {
                ++endQuotePos;
            }
        }
        tableName = normalizedSql.substr(tableNameStart, endQuotePos - tableNameStart);
        if (quoteChar == '\'') {
            // 还原 '' 为单引号
            std::string cleanTableName;
            for (size_t i = 0; i < tableName.length(); ++i) {
                if (tableName[i] == '\'' && i + 1 < tableName.length() &&
                    tableName[i + 1] == '\'') {
                    cleanTableName += '\'';
                    ++i;
                } else {
                    cleanTableName += tableName[i];
                }
            }
            tableName = cleanTableName;
        }
    } else {
        // 非引号包裹：到空白/逗号/分号/左括号为止
        size_t i = tableNameStart;
        while (i < normalizedSql.length() &&
               !std::isspace(static_cast<unsigned char>(normalizedSql[i])) &&
               normalizedSql[i] != ',' && normalizedSql[i] != ';' && normalizedSql[i] != '(') {
            ++i;
        }
        tableName = normalizedSql.substr(tableNameStart, i - tableNameStart);
    }
    return tableName;
}

// 判断表名是否合法（支持中文表名）
// 规则：非空；不含 ; ' " 空白 * \ / ；不含连续 .. 与 -- ；长度 ≤64 ；
//       不以数字开头；字符仅限字母/数字/下划线/点/短横线/UTF-8 中文
bool SQLValidator::isValidTableName(const std::string& tableName) {
    // 1. 非空
    if (tableName.empty()) {
        return false;
    }
    // 2. 非法字符
    for (unsigned char c : tableName) {
        if (c == ';' || c == '\'' || c == '"' || std::isspace(c) ||
            c == '*' || c == '\\' || c == '/') {
            return false;
        }
    }
    // 3. 连续特殊字符
    for (size_t i = 0; i + 1 < tableName.length(); ++i) {
        if (tableName[i] == '.' && tableName[i + 1] == '.') {
            return false;
        }
        if (tableName[i] == '-' && tableName[i + 1] == '-') {
            return false;
        }
    }
    // 4. 长度限制（MySQL 表名上限 64）
    if (tableName.length() > 64) {
        return false;
    }
    // 5. 不能以数字开头（可中文开头）
    if (std::isdigit(static_cast<unsigned char>(tableName[0]))) {
        return false;
    }
    // 6. 逐字符校验：ASCII 限字母/数字/下划线/点/短横线；非 ASCII 按 UTF-8 三字节中文放行
    for (size_t i = 0; i < tableName.length(); ) {
        unsigned char c = static_cast<unsigned char>(tableName[i]);
        if (c <= 127) {
            if (std::isalpha(c) || std::isdigit(c) || c == '_' || c == '.' || c == '-') {
                ++i;
                continue;
            } else {
                return false;
            }
        }
        // UTF-8 三字节序列（中文）
        if ((c & 0xF0) == 0xE0 && i + 2 < tableName.length()) {
            unsigned char c2 = static_cast<unsigned char>(tableName[i + 1]);
            unsigned char c3 = static_cast<unsigned char>(tableName[i + 2]);
            if ((c2 & 0xC0) == 0x80 && (c3 & 0xC0) == 0x80) {
                i += 3;
                continue;
            }
        }
        return false;
    }
    return true;
}

// 判断列名是否合法（规则同表名）
bool SQLValidator::isValidColumnName(const std::string& columnName) {
    return isValidTableName(columnName);
}

} // namespace chat2Data

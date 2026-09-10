#include "sqliteDatabase.h"
#include <bite_scaffold/log.h>
#include "../../common/errorHandler.h"
#include "../../common/utils.h"

namespace databaseService {

//////////////////////////////////// ParamBinder 实现 ////////////////////////////////////

SQLiteDatabase::ParamBinder::ParamBinder(sqlite3* db, const std::string& sql,
                                         const std::vector<PreparedParam>& params) {
    // 1. 编译SQL语句
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &_stmt, nullptr);
    if (rc != SQLITE_OK) {
        ERR("SQLite prepare failed: {}", sqlite3_errmsg(db));
        return;
    }
    // 2. 绑定参数（索引从1开始；TRANSIENT 让 sqlite 复制字符串值）
    for (size_t i = 0; i < params.size(); ++i) {
        const PreparedParam& param = params[i];
        int index = static_cast<int>(i + 1);
        switch (param.getType()) {
            case ParamType::Null:
                sqlite3_bind_null(_stmt, index);
                break;
            case ParamType::Int:
                sqlite3_bind_int64(_stmt, index, param.getIntValue());
                break;
            case ParamType::Double:
                sqlite3_bind_double(_stmt, index, param.getDoubleValue());
                break;
            case ParamType::String:
                sqlite3_bind_text(_stmt, index, param.getStringValue().c_str(), -1,
                                  SQLITE_TRANSIENT);
                break;
            case ParamType::Bool:
                sqlite3_bind_int(_stmt, index, param.getBoolValue() ? 1 : 0);
                break;
        }
    }
}

SQLiteDatabase::ParamBinder::~ParamBinder() {
    if (_stmt != nullptr) {
        sqlite3_finalize(_stmt);
        _stmt = nullptr;
    }
}

sqlite3_stmt* SQLiteDatabase::ParamBinder::getStmt() const { return _stmt; }

int SQLiteDatabase::ParamBinder::step() {
    // 1. 检查预编译语句句柄是否有效
    if (_stmt == nullptr) {
        return SQLITE_ERROR;
    }
    // 2. 执行sql语句
    int rc = sqlite3_step(_stmt);
    _executed = true;
    // 3. 获取查询结果列数
    _columnCount = sqlite3_column_count(_stmt);
    return rc;
}

//////////////////////////////////// SQLiteDatabase 实现 ///////////////////////////////////

SQLiteDatabase::SQLiteDatabase(const SQLiteConfig& config)
    : _config(config) {
}

SQLiteDatabase::~SQLiteDatabase() {
    disconnect();
}

// 打开数据库文件句柄
bool SQLiteDatabase::initDatabase() {
    // 1. 检测sqlite是否已连接
    if (_db != nullptr) { return true; }
    // 2. 打开sqlite数据库（FULLMUTEX：多线程模式）
    int rc = sqlite3_open_v2(
        _config.dbPath.c_str(),
        &_db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr);
    if (rc != SQLITE_OK) {
        ERR("SQLite open failed: {}", sqlite3_errmsg(_db));
        closeDatabase();
        return false;
    }
    return true;
}

void SQLiteDatabase::closeDatabase() {
    // 1. 检测sqlite是否已连接
    if (_db == nullptr) { return; }
    // 2. 关闭sqlite数据库
    sqlite3_close(_db);
    _db = nullptr;
    _connected = false;
}

// 连接（先校验配置：.db 后缀 + 文件存在，再打开句柄）
bool SQLiteDatabase::connect() {
    // 1. 检测sqlite是否已连接
    if (_connected) { return true; }
    // 2. 检测配置是否有效
    if (!_config.validConfig()) {
        ERR("SQLite connect failed: invalid config");
        return false;
    }
    // 3. 初始化并打开sqlite数据库
    if (!initDatabase()) { return false; }
    // 4. sqlite连接成功，设置连接状态
    _connected = true;
    INF("SQLite connected successfully to {}", _config.dbPath);
    return true;
}

void SQLiteDatabase::disconnect() {
    if (!_connected) { return; }
    closeDatabase();
    INF("SQLite disconnected");
}

// 检测连接是否有效（SELECT 1 探活）
bool SQLiteDatabase::ping() {
    // 1. 检测sqlite是否已连接
    if (!_connected || _db == nullptr) { return false; }
    // 2. 执行查询语句
    char* errMsg = nullptr;
    int rc = sqlite3_exec(_db, "SELECT 1", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        ERR("SQLite ping failed: {}", errMsg);
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

// 执行查询语句（列类型优先 decltype，BLOB→base64）
std::shared_ptr<QueryResult> SQLiteDatabase::executeQuery(const std::string& sql) {
    // 1. 创建保存sql执行结果的对象
    auto result = std::make_shared<QueryResult>();
    // 2. 检测sqlite是否已连接
    if (!_connected || _db == nullptr) {
        result->_success = false;
        result->_errorMsg = "Not connected to SQLite";
        return result;
    }
    // 3. 准备预编译语句
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        result->_success = false;
        result->_errorMsg = sqlite3_errmsg(_db);
        ERR("SQLite prepare failed: {}", result->_errorMsg);
        return result;
    }
    // 4. 获取查询结果集的列信息并保存（decltype 为建表声明类型；表达式列退回 column_type）
    int columnCount = sqlite3_column_count(stmt);
    for (int i = 0; i < columnCount; ++i) {
        result->_columns.push_back(sqlite3_column_name(stmt, i));
        const char* declType = sqlite3_column_decltype(stmt, i);
        if (declType) {
            result->_columnTypes.push_back(declType);
        } else {
            int type = sqlite3_column_type(stmt, i);
            switch (type) {
                case SQLITE_INTEGER: result->_columnTypes.push_back("INTEGER"); break;
                case SQLITE_FLOAT:   result->_columnTypes.push_back("REAL");    break;
                case SQLITE_TEXT:    result->_columnTypes.push_back("TEXT");    break;
                case SQLITE_BLOB:    result->_columnTypes.push_back("BLOB");    break;
                case SQLITE_NULL:
                default:             result->_columnTypes.push_back("NULL");    break;
            }
        }
    }
    // 5. 执行查询操作，逐行获取结果
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::vector<std::string> rowData;
        for (int i = 0; i < columnCount; ++i) {
            int type = sqlite3_column_type(stmt, i);
            // 根据该列的类型，将结果转换为字符串
            if (type == SQLITE_NULL) {
                rowData.push_back("");
            } else if (type == SQLITE_INTEGER) {
                rowData.push_back(std::to_string(sqlite3_column_int64(stmt, i)));
            } else if (type == SQLITE_FLOAT) {
                rowData.push_back(std::to_string(sqlite3_column_double(stmt, i)));
            } else if (type == SQLITE_BLOB) {
                // BLOB → base64：BLOB 可能含非法 UTF-8 字节，直塞 protobuf 字符串会校验失败
                const void* blobData = sqlite3_column_blob(stmt, i);
                int blobSize = sqlite3_column_bytes(stmt, i);
                std::vector<char> blobVec(reinterpret_cast<const char*>(blobData),
                                          reinterpret_cast<const char*>(blobData) + blobSize);
                rowData.push_back(chat2Data::Utils::base64Encode(blobVec));
            } else {
                rowData.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, i)));
            }
        }
        result->_rows.push_back(rowData);
    }
    // 6. 关闭预编译句柄
    sqlite3_finalize(stmt);
    // 7. 设置查询成功并返回
    result->_success = true;
    return result;
}

// 执行修改语句（sqlite3_exec + 受影响行数）
std::shared_ptr<QueryResult> SQLiteDatabase::executeModify(const std::string& sql) {
    // 1. 创建保存sql执行结果的对象
    auto result = std::make_shared<QueryResult>();
    // 2. 检测sqlite是否已连接
    if (!_connected || _db == nullptr) {
        result->_success = false;
        result->_errorMsg = "Not connected to SQLite";
        return result;
    }
    // 3. 执行修改语句
    char* errMsg = nullptr;
    int rc = sqlite3_exec(_db, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        result->_success = false;
        result->_errorMsg = errMsg;
        ERR("SQLite exec failed: {}", result->_errorMsg);
        sqlite3_free(errMsg);
        return result;
    }
    // 4. 获取受影响的行数
    result->_affectedRows = sqlite3_changes(_db);
    // 5. 设置修改成功并返回
    result->_success = true;
    return result;
}

// 执行预处理查询语句（ParamBinder 一体化 prepare+bind，step 循环取行）
std::shared_ptr<QueryResult> SQLiteDatabase::executePreparedQuery(
    const std::string& sql, const std::vector<PreparedParam>& params) {
    // 1. 创建保存sql执行结果的对象
    auto result = std::make_shared<QueryResult>();
    // 2. 检测sqlite是否已连接
    if (!_connected || _db == nullptr) {
        result->_success = false;
        result->_errorMsg = "Not connected to SQLite";
        return result;
    }
    // 3. 准备预编译语句，并绑定参数
    ParamBinder binder(_db, sql, params);
    sqlite3_stmt* stmt = binder.getStmt();
    if (stmt == nullptr) {
        result->_success = false;
        result->_errorMsg = "Failed to prepare statement";
        return result;
    }
    // 4. 获取查询结果集的列信息
    int columnCount = sqlite3_column_count(stmt);
    for (int i = 0; i < columnCount; ++i) {
        result->_columns.push_back(sqlite3_column_name(stmt, i));
        int type = sqlite3_column_type(stmt, i);
        std::string typeName;
        switch (type) {
            case SQLITE_INTEGER: typeName = "INTEGER"; break;
            case SQLITE_FLOAT:   typeName = "REAL";    break;
            case SQLITE_TEXT:    typeName = "TEXT";    break;
            case SQLITE_BLOB:    typeName = "BLOB";    break;
            case SQLITE_NULL:
            default:             typeName = "NULL";    break;
        }
        result->_columnTypes.push_back(typeName);
    }
    // 5. 执行查询
    int rc = binder.step();
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        result->_success = false;
        result->_errorMsg = sqlite3_errmsg(_db);
        ERR("SQLite step failed: {}", result->_errorMsg);
        return result;
    }
    // 6. 逐行获取结果
    while (rc == SQLITE_ROW) {
        std::vector<std::string> rowData;
        for (int i = 0; i < columnCount; ++i) {
            int type = sqlite3_column_type(stmt, i);
            if (type == SQLITE_NULL) {
                rowData.push_back("");
            } else if (type == SQLITE_INTEGER) {
                rowData.push_back(std::to_string(sqlite3_column_int64(stmt, i)));
            } else if (type == SQLITE_FLOAT) {
                rowData.push_back(std::to_string(sqlite3_column_double(stmt, i)));
            } else {
                rowData.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, i)));
            }
        }
        result->_rows.push_back(rowData);
        rc = binder.step();
    }
    // 7. 设置查询成功并返回
    result->_success = true;
    return result;
}

// 执行预处理修改语句
std::shared_ptr<QueryResult> SQLiteDatabase::executePreparedModify(
    const std::string& sql, const std::vector<PreparedParam>& params) {
    // 1. 创建保存sql执行结果的对象
    auto result = std::make_shared<QueryResult>();
    // 2. 检测sqlite是否已连接
    if (!_connected || _db == nullptr) {
        result->_success = false;
        result->_errorMsg = "Not connected to SQLite";
        return result;
    }
    // 3. 准备预编译语句，并绑定参数
    ParamBinder binder(_db, sql, params);
    sqlite3_stmt* stmt = binder.getStmt();
    if (stmt == nullptr) {
        result->_success = false;
        result->_errorMsg = "Failed to prepare statement";
        return result;
    }
    // 4. 执行修改操作
    int rc = binder.step();
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        result->_success = false;
        result->_errorMsg = sqlite3_errmsg(_db);
        ERR("SQLite step failed: {}", result->_errorMsg);
        return result;
    }
    // 5. 获取受影响的行数
    result->_affectedRows = sqlite3_changes(_db);
    // 6. 设置修改成功并返回
    result->_success = true;
    return result;
}

// 事务：BEGIN/COMMIT/ROLLBACK 文本 SQL（SQLite 无 autocommit API）
bool SQLiteDatabase::beginTransaction() {
    // 1. 检查sqlite连接是否有效
    if (!_connected || _db == nullptr) {
        return false;
    }
    // 2. 执行开始事务sql语句
    char* errMsg = nullptr;
    int rc = sqlite3_exec(_db, "BEGIN TRANSACTION", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        ERR("SQLite begin transaction failed: {}", errMsg);
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool SQLiteDatabase::commit() {
    if (!_connected || _db == nullptr) {
        return false;
    }
    char* errMsg = nullptr;
    int rc = sqlite3_exec(_db, "COMMIT", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        ERR("SQLite commit failed: {}", errMsg);
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool SQLiteDatabase::rollback() {
    if (!_connected || _db == nullptr) {
        return false;
    }
    char* errMsg = nullptr;
    int rc = sqlite3_exec(_db, "ROLLBACK", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        ERR("SQLite rollback failed: {}", errMsg);
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

// 获取数据库表列表（sqlite_master 过滤 sqlite_ 内部表）
std::vector<std::string> SQLiteDatabase::listTables() {
    // 1. 构造查询表列表的SQL语句
    std::vector<std::string> tables;
    std::string sql =
        "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'";
    // 2. 执行查询语句
    auto result = executeQuery(sql);
    if (result->success()) {
        // 3. 解析查询结果
        tables.reserve(result->_rows.size());
        for (const auto& row : result->_rows) {
            if (!row.empty()) {
                tables.push_back(row[0]);
            }
        }
    }
    return tables;
}

// 获取表结构（PRAGMA table_info：cid/name/type/notnull/dflt_value/pk）
std::vector<ColumnInfo> SQLiteDatabase::getTableStruct(const std::string& tableName) {
    std::vector<ColumnInfo> columns;
    std::string sql = "PRAGMA table_info(" + quoteIdentifier(tableName) + ")";
    auto result = executeQuery(sql);
    if (!result->success()) {
        return columns;
    }
    for (const auto& row : result->_rows) {
        if (row.size() >= 6) {
            ColumnInfo col;
            col.name = row[1];
            col.type = row[2];
            col.nullable = (row[3] == "0");     // notnull=0 才可空（与 MySQL 的 YES 反向）
            col.primaryKey = (row[5] == "1");
            col.defaultValue = row[4];
            columns.push_back(col);
        }
    }
    return columns;
}

// 转义字段名（双引号包裹，已包裹不重复）
std::string SQLiteDatabase::quoteIdentifier(const std::string& identifier) {
    if (!identifier.empty() && identifier.front() == '"' && identifier.back() == '"') {
        return identifier;
    }
    return "\"" + identifier + "\"";
}

// Excel类型 → SQLite类型：BOOLEAN→INTEGER、DATE→TEXT（SQLite 类型亲和性限制）
std::string SQLiteDatabase::convertExcelTypeToSql(const std::string& excelType) {
    if (excelType == "BOOLEAN") {
        return "INTEGER";
    } else if (excelType == "DATE") {
        return "TEXT";
    }
    return excelType;
}

DBType SQLiteDatabase::getDatabaseType() const {
    return DBType::SQLITE;
}

} // namespace databaseService

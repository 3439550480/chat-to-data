#include <sstream>
#include <cstring>
#include <bite_scaffold/log.h>
#include "../../common/errorHandler.h"
#include "../../common/utils.h"
#include "mysqlDatabase.h"

namespace databaseService {

/////////////////////////////////// ParamBinder 实现 ///////////////////////////////////

MySQLDatabase::ParamBinder::ParamBinder(const std::string& sql,
                                        const std::vector<PreparedParam>& params)
    : _paramCount(params.size()) {
    if (_paramCount == 0) {
        return;
    }
    // 预留内存空间
    _binds.resize(_paramCount);
    memset(_binds.data(), 0, _paramCount * sizeof(MYSQL_BIND));
    _longBuffer.resize(_paramCount);
    _doubleBuffer.resize(_paramCount);
    _stringLengthBuffer.resize(_paramCount);
    // 课件问题㊳修复：字符串参数 push_back 进 _stringBuffer 后立即取 c_str() 存入
    // _binds——后续 String 参数再 push_back 触发扩容时，前面已存指针全部悬空。
    // 一次性 reserve(_paramCount) 后元素地址永不变迁，指针恒有效
    _stringBuffer.reserve(_paramCount);
    _boolBuffer = new bool[_paramCount];
    memset(_boolBuffer, 0, _paramCount * sizeof(bool));
    _nullBuffer = new bool[_paramCount];
    memset(_nullBuffer, 0, _paramCount * sizeof(bool));
    // 绑定参数
    for (size_t i = 0; i < _paramCount; ++i) {
        bindParam(i, params[i]);
    }
}

MySQLDatabase::ParamBinder::~ParamBinder() {
    if (_paramCount > 0) {
        delete[] _boolBuffer;
        delete[] _nullBuffer;
    }
}

MYSQL_BIND* MySQLDatabase::ParamBinder::getBinds() { return _binds.data(); }
size_t MySQLDatabase::ParamBinder::getParamCount() const { return _paramCount; }

void MySQLDatabase::ParamBinder::bindParam(size_t index, const PreparedParam& param) {
    switch (param.getType()) {
        case ParamType::Null:
            _binds[index].buffer_type = MYSQL_TYPE_NULL;
            _binds[index].is_null = &_nullBuffer[index];
            _nullBuffer[index] = 1;
            break;
        case ParamType::Int:
            _binds[index].buffer_type = MYSQL_TYPE_LONGLONG;
            _longBuffer[index] = param.getIntValue();
            _binds[index].buffer = &_longBuffer[index];
            _binds[index].is_null = &_nullBuffer[index];
            _nullBuffer[index] = 0;
            break;
        case ParamType::Double:
            _binds[index].buffer_type = MYSQL_TYPE_DOUBLE;
            _doubleBuffer[index] = param.getDoubleValue();
            _binds[index].buffer = &_doubleBuffer[index];
            _binds[index].is_null = &_nullBuffer[index];
            _nullBuffer[index] = 0;
            break;
        case ParamType::String:
            _binds[index].buffer_type = MYSQL_TYPE_STRING;
            _stringBuffer.push_back(param.getStringValue());   // ㊳：reserve 后地址稳定
            _stringLengthBuffer[index] =
                static_cast<unsigned long>(_stringBuffer.back().length());
            _binds[index].buffer = const_cast<char*>(_stringBuffer.back().c_str());
            _binds[index].buffer_length = _stringLengthBuffer[index];
            _binds[index].is_null = &_nullBuffer[index];
            _nullBuffer[index] = 0;
            break;
        case ParamType::Bool:
            _binds[index].buffer_type = MYSQL_TYPE_TINY;
            _boolBuffer[index] = param.getBoolValue();
            _binds[index].buffer = &_boolBuffer[index];
            _binds[index].is_null = &_nullBuffer[index];
            _nullBuffer[index] = 0;
            break;
    }
}

/////////////////////////////////// ResultBinder 实现 ///////////////////////////////////

MySQLDatabase::ResultBinder::ResultBinder(MYSQL_STMT* stmt, MYSQL_RES* meta,
                                          std::shared_ptr<QueryResult> result)
    : _stmt(stmt), _meta(meta), _result(result) {
    _fieldCount = mysql_num_fields(_meta);
}

MySQLDatabase::ResultBinder::~ResultBinder() {
    if (_fieldCount > 0) {
        delete[] _boolBuffer;
        delete[] _nullBuffer;
    }
}

bool MySQLDatabase::ResultBinder::bind() {
    if (_fieldCount == 0) {
        return false;
    }
    // 预留空间
    _binds.resize(_fieldCount);
    memset(_binds.data(), 0, _fieldCount * sizeof(MYSQL_BIND));
    // 课件问题㊴修复：课件直接把空串的 data() 交给 MySQL 写最多 1024 字节——
    // SSO 空串的内部缓冲被越界写，属内存损坏。字符串缓冲改为在下方按列【动态分配】
    _stringBuffer.resize(_fieldCount);          // 占位，默认分支按列声明长度 assign
    _stringLengthBuffer.resize(_fieldCount);
    _longBuffer.resize(_fieldCount);
    _doubleBuffer.resize(_fieldCount);
    _boolBuffer = new bool[_fieldCount];
    memset(_boolBuffer, 0, _fieldCount * sizeof(bool));
    _nullBuffer = new bool[_fieldCount];
    memset(_nullBuffer, 0, _fieldCount * sizeof(bool));
    // 获取字段信息并按类型绑定
    MYSQL_FIELD* fields = mysql_fetch_fields(_meta);
    for (size_t i = 0; i < _fieldCount; ++i) {
        _result->_columns.push_back(fields[i].name);
        switch (fields[i].type) {
            case MYSQL_TYPE_SHORT:
            case MYSQL_TYPE_LONG:
            case MYSQL_TYPE_LONGLONG:
            case MYSQL_TYPE_INT24:
                _binds[i].buffer_type = MYSQL_TYPE_LONGLONG;
                _binds[i].buffer = &_longBuffer[i];
                _binds[i].is_null = &_nullBuffer[i];
                _result->_columnTypes.push_back("BIGINT");
                break;
            case MYSQL_TYPE_DOUBLE:
            case MYSQL_TYPE_DECIMAL:
            case MYSQL_TYPE_FLOAT:
                _binds[i].buffer_type = MYSQL_TYPE_DOUBLE;
                _binds[i].buffer = &_doubleBuffer[i];
                _binds[i].is_null = &_nullBuffer[i];
                _result->_columnTypes.push_back("DOUBLE");
                break;
            case MYSQL_TYPE_TINY:
                _binds[i].buffer_type = MYSQL_TYPE_TINY;
                _binds[i].buffer = &_boolBuffer[i];
                _binds[i].is_null = &_nullBuffer[i];
                _result->_columnTypes.push_back("BOOLEAN");
                break;
            default:
                // 课件问题㊷修复：字符串结果缓冲按【列声明长度】动态分配（clamp 1024~65536），
                // 替代课件的固定 1024——否则长文本列（VARCHAR(4000)/TEXT）在预处理路径
                // 会被静默截断（对比：executeQuery 走 mysql_fetch_row 不受此限，更难排查）
                {
                    long columnBound = static_cast<long>(fields[i].length);  // 声明字节宽度
                    if (columnBound < 1024) { columnBound = 1024; }
                    if (columnBound > 65536) { columnBound = 65536; }        // 上限防 TEXT 的 4G 声明
                    _stringBuffer[i].assign(columnBound, '\0');
                    _binds[i].buffer_type = MYSQL_TYPE_STRING;
                    _binds[i].buffer = _stringBuffer[i].data();              // C++17 起非 const data()
                    _binds[i].buffer_length = columnBound;
                    _binds[i].length = &(_stringLengthBuffer[i]);
                    _binds[i].is_null = &_nullBuffer[i];
                    _result->_columnTypes.push_back("VARCHAR");
                }
                break;
        }
    }
    // 绑定结果
    if (mysql_stmt_bind_result(_stmt, _binds.data()) != 0) {
        ERR("MySQL stmt bind result failed: {}", mysql_stmt_error(_stmt));
        return false;
    }
    // 存储结果
    if (mysql_stmt_store_result(_stmt) != 0) {
        ERR("MySQL stmt store result failed: {}", mysql_stmt_error(_stmt));
        return false;
    }
    // 逐行获取结果
    while (mysql_stmt_fetch(_stmt) == 0) {
        std::vector<std::string> rowData;
        for (size_t i = 0; i < _fieldCount; ++i) {
            if (_nullBuffer[i]) {
                rowData.push_back("");
            } else {
                switch (fields[i].type) {
                    case MYSQL_TYPE_SHORT:
                    case MYSQL_TYPE_LONG:
                    case MYSQL_TYPE_LONGLONG:
                    case MYSQL_TYPE_INT24:
                        rowData.push_back(std::to_string(_longBuffer[i]));
                        break;
                    case MYSQL_TYPE_DOUBLE:
                    case MYSQL_TYPE_DECIMAL:
                    case MYSQL_TYPE_FLOAT:
                        rowData.push_back(std::to_string(_doubleBuffer[i]));
                        break;
                    case MYSQL_TYPE_TINY:
                        rowData.push_back(_boolBuffer[i] ? "1" : "0");
                        break;
                    default:
                        rowData.push_back(_stringBuffer[i].substr(0, _stringLengthBuffer[i]));
                        break;
                }
            }
        }
        _result->_rows.push_back(rowData);
    }
    return true;
}

/////////////////////////////////// MySQLDatabase 实现 ///////////////////////////////////

MySQLDatabase::MySQLDatabase(const MySQLConfig& config)
    : _config(config) {
}

MySQLDatabase::~MySQLDatabase() {
    disconnect();
}

// 初始化MySQL
bool MySQLDatabase::initMysql() {
    // 1. 检测是否已初始化MySQL
    if (_mysql != nullptr) { return true; }
    // 2. 初始化MySQL
    _mysql = mysql_init(nullptr);
    if (_mysql == nullptr) {
        ERR("MySQL init failed");
        return false;
    }
    return true;
}

void MySQLDatabase::closeMysql() {
    // 1. 检测是否初始化MySQL
    if (_mysql == nullptr) { return; }
    // 2. 关闭MySQL连接
    mysql_close(_mysql);
    _mysql = nullptr;
    _connected = false;
}

// 连接MySQL（超时/字符集/SSL 选项 → real_connect）
bool MySQLDatabase::connect() {
    if (_connected) { return true; }
    if (!initMysql()) { return false; }
    unsigned int timeout = static_cast<unsigned int>(_config.timeout);
    mysql_options(_mysql, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    mysql_options(_mysql, MYSQL_SET_CHARSET_NAME, _config.charset.c_str());
    if (_config.hasSSL) {
        mysql_options(_mysql, MYSQL_OPT_SSL_CA, _config.sslCaCert.c_str());
        mysql_options(_mysql, MYSQL_OPT_SSL_CERT, _config.sslCert.c_str());
        mysql_options(_mysql, MYSQL_OPT_SSL_KEY, _config.sslKey.c_str());
    }
    MYSQL* conn = mysql_real_connect(
        _mysql,
        _config.host.c_str(),
        _config.username.c_str(),
        _config.password.c_str(),
        _config.database.c_str(),
        _config.port,
        nullptr,
        0);
    if (conn == nullptr) {
        ERR("MySQL connect failed: {}", mysql_error(_mysql));
        closeMysql();
        return false;
    }
    _connected = true;
    INF("MySQL connected successfully to {}:{}/{}",
        _config.host, _config.port, _config.database);
    return true;
}

// 断开连接
void MySQLDatabase::disconnect() {
    if (!_connected) { return; }
    closeMysql();
    INF("MySQL disconnected");
}

// 检查连接有效性
bool MySQLDatabase::ping() {
    if (!_connected || _mysql == nullptr) { return false; }
    if (mysql_ping(_mysql) != 0) {
        ERR("MySQL ping failed: {}", mysql_error(_mysql));
        _connected = false;
        return false;
    }
    return true;
}

// 执行查询语句（BLOB→base64 转义，防 protobuf UTF-8 校验失败）
std::shared_ptr<QueryResult> MySQLDatabase::executeQuery(const std::string& sql) {
    auto result = std::make_shared<QueryResult>();
    if (!_connected || _mysql == nullptr) {
        result->_success = false;
        result->_errorMsg = "Not connected to MySQL";
        return result;
    }
    int queryResult = mysql_real_query(_mysql, sql.c_str(), sql.length() + 1);
    if (queryResult != 0) {
        result->_success = false;
        result->_errorMsg = mysql_error(_mysql);
        ERR("MySQL query failed: {}", result->_errorMsg);
        return result;
    }
    MYSQL_RES* res = mysql_store_result(_mysql);
    if (res == nullptr) {
        result->_success = false;
        result->_errorMsg = mysql_error(_mysql);
        return result;
    }
    unsigned int numFields = mysql_num_fields(res);
    MYSQL_FIELD* fields = mysql_fetch_fields(res);
    for (unsigned int i = 0; i < numFields; ++i) {
        result->_columns.push_back(fields[i].name);
        result->_columnTypes.push_back(convertColumnType(fields[i].type, fields[i].charsetnr));
    }
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr) {
        std::vector<std::string> rowData;
        for (unsigned int i = 0; i < numFields; ++i) {
            if (row[i] == nullptr) {
                rowData.push_back("");
            } else if (fields[i].type == MYSQL_TYPE_BLOB) {
                // BLOB 可能含非法 UTF-8 字节，proto3 会校验失败 → base64 编码转文本
                // charsetnr==63 是 binary collation（真 BLOB），否则按 TEXT 直取
                unsigned long length = mysql_fetch_lengths(res)[i];
                if (fields[i].charsetnr == 63) {
                    std::vector<char> blobVec(row[i], row[i] + length);
                    rowData.push_back(chat2Data::Utils::base64Encode(blobVec));
                } else {
                    rowData.push_back(std::string(row[i], length));
                }
            } else {
                rowData.push_back(row[i]);
            }
        }
        result->_rows.push_back(rowData);
    }
    mysql_free_result(res);
    result->_success = true;
    return result;
}

// 执行修改语句
std::shared_ptr<QueryResult> MySQLDatabase::executeModify(const std::string& sql) {
    auto result = std::make_shared<QueryResult>();
    if (!_connected || _mysql == nullptr) {
        result->_success = false;
        result->_errorMsg = "Not connected to MySQL";
        return result;
    }
    int queryResult = mysql_real_query(_mysql, sql.c_str(), sql.length() + 1);
    if (queryResult != 0) {
        result->_success = false;
        result->_errorMsg = mysql_error(_mysql);
        ERR("MySQL modify failed: {}", result->_errorMsg);
        return result;
    }
    result->_affectedRows = static_cast<int>(mysql_affected_rows(_mysql));
    result->_success = true;
    return result;
}

// 执行预处理查询语句
std::shared_ptr<QueryResult> MySQLDatabase::executePreparedQuery(
    const std::string& sql, const std::vector<PreparedParam>& params) {
    auto result = std::make_shared<QueryResult>();
    if (!_connected || _mysql == nullptr) {
        result->_success = false;
        result->_errorMsg = "Not connected to MySQL";
        return result;
    }
    MYSQL_STMT* stmt = mysql_stmt_init(_mysql);
    if (stmt == nullptr) {
        result->_success = false;
        result->_errorMsg = mysql_error(_mysql);
        return result;
    }
    if (mysql_stmt_prepare(stmt, sql.c_str(), sql.length() + 1) != 0) {
        result->_success = false;
        result->_errorMsg = mysql_stmt_error(stmt);
        ERR("MySQL stmt prepare failed: {}", result->_errorMsg);
        mysql_stmt_close(stmt);
        return result;
    }
    ParamBinder paramBinder(sql, params);
    if (mysql_stmt_bind_param(stmt, paramBinder.getBinds()) != 0) {
        result->_success = false;
        result->_errorMsg = mysql_stmt_error(stmt);
        ERR("MySQL stmt bind param failed: {}", result->_errorMsg);
        mysql_stmt_close(stmt);
        return result;
    }
    if (mysql_stmt_execute(stmt) != 0) {
        result->_success = false;
        result->_errorMsg = mysql_stmt_error(stmt);
        ERR("MySQL stmt execute failed: {}", result->_errorMsg);
        mysql_stmt_close(stmt);
        return result;
    }
    MYSQL_RES* meta = mysql_stmt_result_metadata(stmt);
    if (meta != nullptr) {
        ResultBinder resultBinder(stmt, meta, result);
        resultBinder.bind();
        mysql_free_result(meta);
    }
    mysql_stmt_close(stmt);
    result->_success = true;
    return result;
}

// 执行预处理修改语句
std::shared_ptr<QueryResult> MySQLDatabase::executePreparedModify(
    const std::string& sql, const std::vector<PreparedParam>& params) {
    auto result = std::make_shared<QueryResult>();
    if (!_connected || _mysql == nullptr) {
        result->_success = false;
        result->_errorMsg = "Not connected to MySQL";
        return result;
    }
    MYSQL_STMT* stmt = mysql_stmt_init(_mysql);
    if (stmt == nullptr) {
        result->_success = false;
        result->_errorMsg = mysql_error(_mysql);
        return result;
    }
    if (mysql_stmt_prepare(stmt, sql.c_str(), sql.length() + 1) != 0) {
        result->_success = false;
        result->_errorMsg = mysql_stmt_error(stmt);
        ERR("MySQL stmt prepare failed: {}", result->_errorMsg);
        mysql_stmt_close(stmt);
        return result;
    }
    ParamBinder paramBinder(sql, params);
    if (mysql_stmt_bind_param(stmt, paramBinder.getBinds()) != 0) {
        result->_success = false;
        result->_errorMsg = mysql_stmt_error(stmt);
        ERR("MySQL stmt bind param failed: {}", result->_errorMsg);
        mysql_stmt_close(stmt);
        return result;
    }
    if (mysql_stmt_execute(stmt) != 0) {
        result->_success = false;
        result->_errorMsg = mysql_stmt_error(stmt);
        ERR("MySQL stmt execute failed: {}", result->_errorMsg);
        mysql_stmt_close(stmt);
        return result;
    }
    result->_affectedRows = static_cast<int>(mysql_stmt_affected_rows(stmt));
    mysql_stmt_close(stmt);
    result->_success = true;
    return result;
}

// 事务：autocommit(0) → commit/rollback → autocommit(1)
bool MySQLDatabase::beginTransaction() {
    if (!_connected || _mysql == nullptr) { return false; }
    if (mysql_autocommit(_mysql, 0) != 0) {
        ERR("MySQL begin transaction failed: {}", mysql_error(_mysql));
        return false;
    }
    return true;
}

bool MySQLDatabase::commit() {
    if (!_connected || _mysql == nullptr) { return false; }
    if (mysql_commit(_mysql) != 0) {
        ERR("MySQL commit failed: {}", mysql_error(_mysql));
        return false;
    }
    mysql_autocommit(_mysql, 1);
    return true;
}

bool MySQLDatabase::rollback() {
    if (!_connected || _mysql == nullptr) { return false; }
    if (mysql_rollback(_mysql) != 0) {
        ERR("MySQL rollback failed: {}", mysql_error(_mysql));
        return false;
    }
    mysql_autocommit(_mysql, 1);
    return true;
}

// 获取表结构（DESC 表：6 列 → ColumnInfo）
std::vector<ColumnInfo> MySQLDatabase::getTableStruct(const std::string& tableName) {
    std::vector<ColumnInfo> columns;
    std::string sql = "DESC " + quoteIdentifier(tableName);
    auto result = executeQuery(sql);
    if (!result->success()) {
        return columns;
    }
    for (const auto& row : result->_rows) {
        if (row.size() >= 6) {
            ColumnInfo col;
            col.name = row[0];
            col.type = row[1];
            col.nullable = (row[2] == "YES");
            col.primaryKey = (row[3] == "PRI");
            col.defaultValue = row[4];
            col.autoIncrement = (row[5].find("auto_increment") != std::string::npos);
            columns.push_back(col);
        }
    }
    return columns;
}

// 获取数据库表列表（SHOW TABLES）
std::vector<std::string> MySQLDatabase::listTables() {
    std::vector<std::string> tables;
    auto result = executeQuery("SHOW TABLES");
    if (result->success()) {
        tables.reserve(result->_rows.size());
        for (const auto& row : result->_rows) {
            if (!row.empty()) {
                tables.push_back(row[0]);
            }
        }
    }
    return tables;
}

// Excel类型 → MySQL类型：BIGINT/DOUBLE/BOOLEAN/DATE/TEXT 直接透传
std::string MySQLDatabase::convertExcelTypeToSql(const std::string& excelType) {
    return excelType;
}

DBType MySQLDatabase::getDatabaseType() const {
    return DBType::MYSQL;
}

// 标识符转义：已包裹则不重复，否则反引号包裹
std::string MySQLDatabase::quoteIdentifier(const std::string& identifier) {
    if (!identifier.empty() && identifier.front() == '`' && identifier.back() == '`') {
        return identifier;
    }
    return "`" + identifier + "`";
}

// MySQL字段类型 → 字符串（charsetnr==63 为 binary collation → 真 BLOB）
std::string MySQLDatabase::convertColumnType(enum_field_types type, unsigned int charsetnr) {
    switch (type) {
        case MYSQL_TYPE_NULL:       return "NULL";
        case MYSQL_TYPE_LONG:       return "INT";
        case MYSQL_TYPE_LONGLONG:   return "BIGINT";
        case MYSQL_TYPE_DOUBLE:     return "DOUBLE";
        case MYSQL_TYPE_STRING:     return "VARCHAR";
        case MYSQL_TYPE_VAR_STRING: return "VARCHAR";
        case MYSQL_TYPE_BLOB:
            return (charsetnr == 63) ? "BLOB" : "TEXT";
        case MYSQL_TYPE_DATE:       return "DATE";
        case MYSQL_TYPE_DATETIME:   return "DATETIME";
        case MYSQL_TYPE_TIMESTAMP:  return "TIMESTAMP";
        default:                    return "UNKNOWN";
    }
}

} // namespace databaseService

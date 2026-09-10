#include "databaseSchema.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <filesystem>
#include <jsoncpp/json/json.h>
#include <bite_scaffold/util.h>

namespace databaseService {

// 验证MySQL配置是否有效
bool MySQLConfig::validConfig() const {
    if (host.empty()) { return false; }
    if (port <= 0 || port > 65535) { return false; }
    if (username.empty()) { return false; }
    if (database.empty()) { return false; }
    if (hasSSL) {
        if (sslCaCert.empty() || sslCert.empty() || sslKey.empty()) {
            return false;
        }
    }
    return true;
}

DBType MySQLConfig::getType() const {
    return DBType::MYSQL;
}

// 验证SQLite配置是否有效：路径非空 + .db 后缀 + 文件已存在
bool SQLiteConfig::validConfig() const {
    // 1. 检测数据库路径是否为空
    if (dbPath.empty()) { return false; }
    // 2. 检测数据库路径对应的文件后缀是否为.db
    if (dbPath.size() < 3 || dbPath.substr(dbPath.size() - 3) != ".db") {
        return false;
    }
    // 3. 检测文件是否存在（连接不隐式建库：SQLite 上传必须先走文件子服务）
    if (!std::filesystem::exists(dbPath)) { return false; }
    return true;
}

DBType SQLiteConfig::getType() const { return DBType::SQLITE; }

// ==================== PreparedParam：五个类型构造 ====================

PreparedParam::PreparedParam() {}
PreparedParam::PreparedParam(std::nullptr_t)
    : _type(ParamType::Null) {}
PreparedParam::PreparedParam(long long value)
    : _type(ParamType::Int), _intValue(value) {}
PreparedParam::PreparedParam(double value)
    : _type(ParamType::Double), _doubleValue(value) {}
PreparedParam::PreparedParam(const std::string& value)
    : _type(ParamType::String), _stringValue(value) {}
PreparedParam::PreparedParam(bool value)
    : _type(ParamType::Bool), _boolValue(value) {}

ParamType PreparedParam::getType() const { return _type; }

// 空串按 NULL 处理（课件语义：Excel 空单元格入库即 NULL）
bool PreparedParam::isNull() const {
    return _type == ParamType::Null ||
           (_type == ParamType::String && _stringValue.empty());
}

long long PreparedParam::getIntValue() const {
    if (_type == ParamType::Int) { return _intValue; }
    if (_type == ParamType::Double) { return static_cast<long long>(_doubleValue); }
    if (_type == ParamType::Bool) { return _boolValue ? 1 : 0; }
    if (_type == ParamType::String) {
        try { return std::stoll(_stringValue); } catch (...) { return 0; }
    }
    return 0;
}

double PreparedParam::getDoubleValue() const {
    if (_type == ParamType::Double) { return _doubleValue; }
    if (_type == ParamType::Int) { return static_cast<double>(_intValue); }
    if (_type == ParamType::Bool) { return _boolValue ? 1.0 : 0.0; }
    if (_type == ParamType::String) {
        try { return std::stod(_stringValue); } catch (...) { return 0.0; }
    }
    return 0.0;
}

std::string PreparedParam::getStringValue() const {
    if (_type == ParamType::String) { return _stringValue; }
    if (_type == ParamType::Int) { return std::to_string(_intValue); }
    if (_type == ParamType::Double) {
        std::ostringstream oss;
        oss << std::setprecision(15) << _doubleValue;
        return oss.str();
    }
    if (_type == ParamType::Bool) { return _boolValue ? "1" : "0"; }
    return "";
}

bool PreparedParam::getBoolValue() const {
    if (_type == ParamType::Bool) { return _boolValue; }
    if (_type == ParamType::Int) { return _intValue != 0; }
    std::string value = _stringValue;
    std::transform(value.begin(), value.end(), value.begin(), ::tolower);
    if (_type == ParamType::String) {
        return value == "1" || value == "true" || value == "yes" || value == "y" || value == "t";
    }
    return false;
}

// ==================== QueryResult ====================

bool QueryResult::success() const { return _success; }
const std::string& QueryResult::errorMsg() const { return _errorMsg; }
int QueryResult::columnCount() const { return static_cast<int>(_columns.size()); }
int QueryResult::rowCount() const { return static_cast<int>(_rows.size()); }

std::vector<std::string> QueryResult::getRow(int index) const {
    if (index < 0 || index >= rowCount()) {
        return {};
    }
    return _rows[index];
}

std::string QueryResult::toJson() const {
    Json::Value jsonObj;
    jsonObj["success"] = _success ? "true" : "false";
    jsonObj["error"] = _errorMsg;
    jsonObj["affectedRows"] = _affectedRows;
    Json::Value columnsArray;
    for (size_t i = 0; i < _columns.size(); ++i) {
        Json::Value colObj;
        colObj["name"] = _columns[i];
        colObj["type"] = _columnTypes[i];
        columnsArray.append(colObj);
    }
    jsonObj["columns"] = columnsArray;
    Json::Value rowsArray;
    for (size_t i = 0; i < _rows.size(); ++i) {
        Json::Value rowArray;
        for (size_t j = 0; j < _rows[i].size(); ++j) {
            rowArray.append(_rows[i][j]);
        }
        rowsArray.append(rowArray);
    }
    jsonObj["rows"] = rowsArray;
    auto result = biteutil::JSON::serialize(jsonObj, true);
    if (result.has_value()) {
        return result.value();
    }
    return "{}";
}

} // namespace databaseService

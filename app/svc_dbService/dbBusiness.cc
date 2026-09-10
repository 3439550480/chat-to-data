#include <sstream>
#include <algorithm>
#include <cctype>
#include <mutex>
#include <chrono>
#include <filesystem>
#include <bite_scaffold/log.h>
#include "dbBusiness.h"
#include "dbConnMgr.h"
#include "dbDriver/databaseFactory.h"
#include "dbDriver/databaseSchema.h"
#include "../common/sqlValidator.h"
#include "../common/errorHandler.h"
#include "../common/utils.h"
#include "../proto/protoCode/fileService.pb.h"
// 课件问题㉚纪律：pb.h 已在前面拉入，fdfs.h 必须在其后 + #undef byte 双保险
#include <bite_scaffold/fdfs.h>
#ifdef byte
#undef byte
#endif

namespace databaseService {

DBBusiness::DBBusiness(std::shared_ptr<DBConnMgr> connMgr,
                       std::shared_ptr<biterpc::SvcChannels> svcChannels,
                       MySQLConfig defaultMySQLConfig)
    : _connMgr(connMgr)
    , _svcChannels(svcChannels) {
    // 创建默认 MySQL 连接（excel_default）：智能 Excel 场景的入库通道
    auto db = DatabaseFactory::getInstance().createDatabase(&defaultMySQLConfig);
    if (!db) {
        ERR("Failed to create default MySQL database");
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::DB_CONNECTION_FAILED);
    }
    if (!db->connect()) {
        ERR("Failed to connect to default MySQL database");
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::DB_CONNECTION_FAILED);
    }
    _connMgr->createConnection("", db, true);
    INF("DBBusiness initialized with default MySQL connection");
}

// 新建数据库连接
std::string DBBusiness::connectDatabase(const std::string& userId,
                                        const chat2Data::DatabaseService::DatabaseConfig& config) {
    DBConfig* dbConfig = nullptr;
    MySQLConfig mysqlConfig;
    SQLiteConfig sqliteConfig;
    std::string sqliteFilePath;
    // 1. 按类型组装驱动配置
    if (config.type() == chat2Data::DatabaseService::DATABASE_TYPE_MYSQL) {
        const auto& mysqlConf = config.mysql_config();
        mysqlConfig.host = mysqlConf.host();
        mysqlConfig.port = mysqlConf.port();
        mysqlConfig.username = mysqlConf.username();
        mysqlConfig.password = mysqlConf.password();
        mysqlConfig.database = mysqlConf.name();
        mysqlConfig.charset = mysqlConf.charset().empty() ? "utf8mb4" : mysqlConf.charset();
        dbConfig = &mysqlConfig;
    } else if (config.type() == chat2Data::DatabaseService::DATABASE_TYPE_SQLITE) {
        // SQLite：用户只给 fileId，需先经文件服务拿到 FastDFS id 再下载到本地
        const auto& sqliteConf = config.sqlite_config();
        std::string fileId = sqliteConf.file_id();
        if (fileId.empty()) {
            throw chat2Data::Chat2DataException(chat2Data::ErrorCode::DB_PARAM_INVALID);
        }
        sqliteFilePath = downloadSQLiteFile(fileId);
        sqliteConfig.dbPath = sqliteFilePath;
        dbConfig = &sqliteConfig;
    } else {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::DB_PARAM_INVALID);
    }
    // 2. 校验配置（工厂内还会再校验一次，此处提前给出明确错误）
    if (!dbConfig->validConfig()) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::DB_PARAM_INVALID);
    }
    // 3. 创建并连接实例
    auto db = DatabaseFactory::getInstance().createDatabase(dbConfig);
    if (!db) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::DB_CONNECTION_FAILED);
    }
    if (!db->connect()) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::DB_CONNECTION_FAILED);
    }
    // 4. 交由连接管理器托管（SQLite 记录本地路径，断开时清理）
    return _connMgr->createConnection(userId, db, false, sqliteFilePath);
}

// 从文件子服务取 SQLite 文件并下载到本地
std::string DBBusiness::downloadSQLiteFile(const std::string& fileId) {
    // 1. 取文件服务通道
    auto fileServiceChannel = _svcChannels->getNode(FLAGS_file_service);
    if (!fileServiceChannel) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::DB_CONNECTION_FAILED);
    }
    // 2. 构建 GetSQLiteFile 请求（向文件服务要 FastDFS 文件id）
    chat2Data::fileService::GetSQLiteFileRequest req;
    req.set_request_id(chat2Data::Utils::generateUuid());
    req.set_session_id(chat2Data::Utils::generateUuid());
    req.set_file_id(fileId);
    // 3. 发起 RPC
    chat2Data::fileService::FileService_Stub stub(fileServiceChannel.get());
    chat2Data::fileService::GetSQLiteFileResponse resp;
    brpc::Controller cntl;
    stub.GetSQLiteFile(&cntl, &req, &resp, nullptr);
    // 4. 校验 RPC 结果
    if (cntl.Failed() || resp.error_code() != 0) {
        ERR("Failed to get SQLite file from fileService: {}",
            cntl.Failed() ? cntl.ErrorText() : resp.error_msg());
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::DB_CONNECTION_FAILED);
    }
    std::string fdfsFileId = resp.result().fdfsfileid();
    // 5. 准备本地目录（可执行文件同级的 sqliteFiles/）
    std::filesystem::path exePath = std::filesystem::current_path();
    std::filesystem::path sqliteDir = exePath / "sqliteFiles";
    std::error_code ec;
    if (!std::filesystem::exists(sqliteDir)) {
        if (!std::filesystem::create_directory(sqliteDir, ec)) {
            ERR("Failed to create sqliteFiles directory: {}", ec.message());
            throw chat2Data::Chat2DataException(chat2Data::ErrorCode::DB_SQLITE_DOWNLOAD_FAILED);
        }
    }
    // 6. 下载文件（文件名 = 业务 fileId，便于断开时按名清理）
    std::string localPath = (sqliteDir / (fileId + ".db")).string();
    if (!bitefdfs::FDFSClient::download_to_file(fdfsFileId, localPath)) {
        ERR("Failed to download SQLite file from FastDFS: {}", fdfsFileId);
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::DB_SQLITE_DOWNLOAD_FAILED);
    }
    INF("SQLite file downloaded: {}", localPath);
    return localPath;
}

// 断开数据库连接
bool DBBusiness::disconnectDatabase(const std::string& connectionId) {
    // 1. 先取出连接信息（判断是否需要清理 SQLite 本地文件）
    auto connInfo = _connMgr->getConnection(connectionId);
    if (connInfo && connInfo->db->getDatabaseType() == DBType::SQLITE &&
        !connInfo->sqliteFilePath.empty()) {
        std::filesystem::path sqlitePath(connInfo->sqliteFilePath);
        std::string baseName = sqlitePath.filename().string();
        std::filesystem::path parentDir = sqlitePath.parent_path();
        std::error_code ec;
        if (std::filesystem::exists(parentDir, ec)) {
            // 删除同名文件（含 SQLite 运行期产生的 -wal/-shm 附属文件）：
            // 课件用精确相等，这里改前缀匹配，避免残留 -wal/-shm
            for (const auto& entry : std::filesystem::directory_iterator(parentDir, ec)) {
                const std::string fileName = entry.path().filename().string();
                if (entry.is_regular_file(ec) && fileName.rfind(baseName, 0) == 0) {
                    std::filesystem::remove(entry.path(), ec);
                    if (ec) {
                        WRN("Failed to delete SQLite file: {}, error: {}",
                            entry.path().string(), ec.message());
                    } else {
                        INF("Deleted SQLite file: {}", entry.path().string());
                    }
                }
            }
        }
    }
    // 2. 清理该连接创建的所有临时表
    deleteTempTablesForConnection(connectionId);
    // 3. 从连接管理器移除（内部会 disconnect）
    return _connMgr->removeConnection(connectionId);
}

// 获取指定连接下的所有临时表
std::vector<std::string> DBBusiness::getConnTempTables(const std::string& connectionId) {
    std::lock_guard<std::mutex> lock(_tempTableMapMutex);
    std::vector<std::string> tempTables;
    auto connIt = _tempTableMap.find(connectionId);
    if (connIt != _tempTableMap.end()) {
        for (const auto& backup : connIt->second) {
            tempTables.push_back(backup.tempTable);
        }
    }
    return tempTables;
}

// 删除指定临时表
bool DBBusiness::deleteTempTable(const std::string& connectionId, const std::string& tempTable) {
    auto db = getDatabase(connectionId);
    if (!db) {
        return false;
    }
    std::string dropSql = "DROP TABLE IF EXISTS " + db->quoteIdentifier(tempTable);
    auto result = db->executeModify(dropSql);
    if (!result->success()) {
        ERR("Failed to delete temp table: {}", tempTable);
        return false;
    }
    INF("Temp table deleted: {}", tempTable);
    return true;
}

// 删除指定连接下所有临时表 + 清理映射
bool DBBusiness::deleteTempTablesForConnection(const std::string& connectionId) {
    // 1. 取出该连接的临时表名（getConnTempTables 自带锁，此处不持锁调用，避免嵌套死锁）
    auto tempTables = getConnTempTables(connectionId);
    // 2. 逐个删除
    for (const auto& tempTable : tempTables) {
        if (!deleteTempTable(connectionId, tempTable)) {
            WRN("Failed to delete temp table: {} for connection: {}", tempTable, connectionId);
        }
    }
    // 3. 清理映射
    std::lock_guard<std::mutex> lock(_tempTableMapMutex);
    _tempTableMap.erase(connectionId);
    return true;
}

// 按连接ID取数据库实例
std::shared_ptr<IDatabase> DBBusiness::getDatabase(const std::string& connectionId) {
    auto connInfo = _connMgr->getConnection(connectionId);
    if (!connInfo) {
        return nullptr;
    }
    return connInfo->db;
}

// 获取数据库表列表
std::vector<std::string> DBBusiness::listTables(const std::string& connectionId) {
    auto db = getDatabase(connectionId);
    if (!db) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::DB_CONNECTION_NOT_EXISTS);
    }
    return db->listTables();
}

// 获取指定表数据（默认读临时表：改过但未提交的数据优先展示）
TableDataResult DBBusiness::getTableData(const std::string& connectionId,
                                         const std::string& tableName,
                                         bool forceOriginal,
                                         int32_t pageNumber, int32_t pageSize) {
    // 1. 取数据库实例
    auto db = getDatabase(connectionId);
    if (!db) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::DB_CONNECTION_NOT_EXISTS);
    }
    // 2. 分页参数归位
    TableDataResult tableResult;
    tableResult.pageSize = pageSize > 0 ? pageSize : 50;
    tableResult.currentPage = pageNumber > 0 ? pageNumber : 1;
    // 3. 确定实际表名：非强制原表时优先查临时表
    std::string actualTableName = tableName;
    if (!forceOriginal) {
        std::lock_guard<std::mutex> lock(_tempTableMapMutex);
        auto connIt = _tempTableMap.find(connectionId);
        if (connIt != _tempTableMap.end()) {
            for (const auto& backup : connIt->second) {
                if (backup.originalTable == tableName) {
                    actualTableName = backup.tempTable;
                    break;
                }
            }
        }
    }
    // 4. 统计总行数
    std::string countSql = "SELECT COUNT(*) FROM " + db->quoteIdentifier(actualTableName);
    auto countResult = db->executeQuery(countSql);
    if (countResult->success() && !countResult->_rows.empty()) {
        tableResult.totalRows = std::stoi(countResult->_rows[0][0]);
    }
    // 5. 分页查询（LIMIT/OFFSET 两库通用）
    tableResult.totalPages =
        (tableResult.totalRows + tableResult.pageSize - 1) / tableResult.pageSize;
    int offset = (tableResult.currentPage - 1) * tableResult.pageSize;
    std::string dataSql = "SELECT * FROM " + db->quoteIdentifier(actualTableName) +
                          " LIMIT " + std::to_string(tableResult.pageSize) +
                          " OFFSET " + std::to_string(offset);
    auto dataResult = db->executeQuery(dataSql);
    if (!dataResult->success()) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::DB_TABLE_DATA_GET_FAILED);
    }
    // 6. 组装数据与列类型
    tableResult.columns = dataResult->_columns;
    tableResult.rows = dataResult->_rows;
    auto columnInfos = db->getTableStruct(actualTableName);
    tableResult.columnTypes.reserve(columnInfos.size());
    for (const auto& col : columnInfos) {
        tableResult.columnTypes.push_back(col.type);
    }
    return tableResult;
}

// 执行SQL语句：校验 → 修改类走临时表沙箱 → 执行
chat2Data::DatabaseService::ExecuteSQLResponse DBBusiness::executeSQL(
    const std::string& connectionId, const std::string& sql) {
    chat2Data::DatabaseService::ExecuteSQLResponse response;
    // 1. 取数据库实例
    auto db = getDatabase(connectionId);
    if (!db) {
        response.set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::DB_CONNECTION_NOT_EXISTS));
        response.set_error_msg("Database connection not exists");
        return response;
    }
    // 2. SQL 三关校验（危险词 / 多语句 / 类型白名单；校验器内部自行规范化）
    //    说明：课件此处变量名 normalizedSql 但从未真正规范化——我们校验原文，
    //    且【执行原文】，避免"校验的是A、执行的是B"这类信任边界裂缝
    if (!chat2Data::SQLValidator::validate(sql)) {
        response.set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::DB_PARAM_INVALID));
        response.set_error_msg("Invalid SQL statement");
        return response;
    }
    // 3. 判定只读/修改
    bool isModify = chat2Data::SQLValidator::isModifySQL(sql);
    std::string sqlToExecute = sql;
    // 4. 修改类 SQL 走临时表沙箱：备份原表 → 表名改指临时表 → 执行
    if (isModify) {
        // 4.1 提取目标表名
        std::string tableName = chat2Data::SQLValidator::extractTableName(sql);
        // 4.2 备份原表为临时表
        std::string tempTableName = backupTable(connectionId, tableName);
        if (tempTableName.empty()) {
            response.set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::DB_BACKUP_TABLE_FAILED));
            response.set_error_msg("Failed to backup table");
            return response;
        }
        // 4.3 改写 SQL：原表名 → 临时表名（㊻ 词边界 + 跳过字符串内部）
        sqlToExecute = replaceTableNameWithTemp(connectionId, sql, tableName);
        INF("Modify SQL redirected to temp table: {} -> {}", tableName, tempTableName);
    }
    // 5. 执行
    std::shared_ptr<QueryResult> result;
    if (isModify) {
        result = db->executeModify(sqlToExecute);
    } else {
        result = db->executeQuery(sqlToExecute);
    }
    // 6. 组装响应（查询返回列与行；修改返回影响行数）
    response.set_error_code(result->success()
        ? 0 : static_cast<int32_t>(chat2Data::ErrorCode::DB_SQL_EXECUTE_FAILED));
    response.set_error_msg(result->success() ? "" : result->_errorMsg);
    response.set_is_query(!isModify);
    response.set_affected_rows(result->_affectedRows);
    for (const auto& col : result->_columns) {
        response.add_columns(col);
    }
    for (const auto& colType : result->_columnTypes) {
        response.add_column_types(colType);
    }
    for (const auto& row : result->_rows) {
        auto* protoRow = response.add_rows();
        for (const auto& cell : row) {
            protoRow->add_cells(cell);
        }
    }
    return response;
}

// 将 SQL 中的原表名替换为临时表名
// 课件问题㊻修复：课件用朴素 find/replace 全串替换——
//   ① 表名是别的标识符子串时误改（"user" 命中 "tbl_user"）
//   ② 字符串字面量内部也照改（SET note='user' 里的 'user' 被换掉，语义被破坏）
// 修复：只替换「独立标识符」且「不在字符串内」的出现
std::string DBBusiness::replaceTableNameWithTemp(const std::string& connectionId,
                                                 const std::string& sql,
                                                 const std::string& originalTable) {
    // 1. 查该连接下原表对应的临时表
    std::string tempTable;
    {
        std::lock_guard<std::mutex> lock(_tempTableMapMutex);
        auto connIt = _tempTableMap.find(connectionId);
        if (connIt == _tempTableMap.end()) {
            return sql;
        }
        for (const auto& backup : connIt->second) {
            if (backup.originalTable == originalTable) {
                tempTable = backup.tempTable;
                break;
            }
        }
    }
    if (tempTable.empty() || originalTable.empty()) {
        return sql;
    }
    // 2. 标识符字符判定（字母/数字/下划线/UTF-8 中文；与表名合法性规则一致）
    auto isIdentChar = [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c >= 0x80;
    };
    // 3. 扫描替换：跳过字符串字面量；只替换词边界匹配
    std::string result;
    result.reserve(sql.size());
    size_t i = 0;
    while (i < sql.length()) {
        // 3.1 字符串字面量整体搬运（内部不做任何替换）
        if (sql[i] == '\'' || sql[i] == '"') {
            char quoteChar = sql[i];
            result += sql[i++];
            while (i < sql.length()) {
                if (sql[i] == '\\' && i + 1 < sql.length()) {
                    result += sql[i++];
                    result += sql[i++];
                } else if (sql[i] == quoteChar) {
                    if (i + 1 < sql.length() && sql[i + 1] == quoteChar) {   // '' 双写
                        result += sql[i++];
                        result += sql[i++];
                    } else {
                        result += sql[i++];
                        break;
                    }
                } else {
                    result += sql[i++];
                }
            }
            continue;
        }
        // 3.2 词边界匹配原表名
        if (sql.compare(i, originalTable.length(), originalTable) == 0) {
            bool leftOk = (i == 0) || !isIdentChar(static_cast<unsigned char>(sql[i - 1]));
            size_t end = i + originalTable.length();
            bool rightOk = (end >= sql.length()) ||
                           !isIdentChar(static_cast<unsigned char>(sql[end]));
            if (leftOk && rightOk) {
                result += tempTable;
                i = end;
                continue;
            }
        }
        result += sql[i++];
    }
    return result;
}

// 构建临时表名（原表名 + _temp + 毫秒时间戳，保证同表多次备份不冲突）
std::string DBBusiness::buildTempTableName(const std::string& originalTable) {
    return originalTable + "_temp";
}

// 备份数据库表：生成临时表并记录映射
// 注：课件语义为"每次修改都从原表重新备份"（同名旧副本先删）——
//     即多次修改不叠加，预览只反映最近一条 SQL。已登记为观察项，如需叠加语义改此处
std::string DBBusiness::backupTable(const std::string& connectionId,
                                    const std::string& tableName) {
    auto db = getDatabase(connectionId);
    if (!db) {
        return "";
    }
    if (tableName.empty()) {
        ERR("Backup table failed: empty table name");
        return "";
    }
    std::lock_guard<std::mutex> lock(_tempTableMapMutex);
    // 1. 同名旧副本先删（保证一对一，避免副本堆积）
    auto connIt = _tempTableMap.find(connectionId);
    if (connIt != _tempTableMap.end()) {
        for (const auto& backup : connIt->second) {
            if (backup.originalTable == tableName) {
                std::string dropOldTemp = "DROP TABLE IF EXISTS " + db->quoteIdentifier(backup.tempTable);
                db->executeModify(dropOldTemp);
            }
        }
    }
    // 2. 生成带时间戳的临时表名
    int64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    std::string tempTableName = buildTempTableName(tableName) + "_" + std::to_string(timestamp);
    // 3. 备份（CREATE TABLE ... AS SELECT：结构+数据，不含索引约束——预览够用）
    std::string backupSql = "CREATE TABLE " + db->quoteIdentifier(tempTableName) +
                            " AS SELECT * FROM " + db->quoteIdentifier(tableName);
    auto result = db->executeModify(backupSql);
    if (!result->success()) {
        ERR("Failed to backup table: {} -> {}, error: {}",
            tableName, tempTableName, result->_errorMsg);
        return "";
    }
    // 4. 记录映射（覆盖该表旧记录，保持一对一）
    auto& backups = _tempTableMap[connectionId];
    backups.erase(std::remove_if(backups.begin(), backups.end(),
                                 [&tableName](const TempTableBackup& b) {
                                     return b.originalTable == tableName;
                                 }),
                  backups.end());
    TempTableBackup backup;
    backup.originalTable = tableName;
    backup.tempTable = tempTableName;
    backup.backupTime = timestamp;
    backups.push_back(backup);
    INF("Table backed up: {} -> {}", tableName, tempTableName);
    return tempTableName;
}

// 获取表结构（格式化字符串，供 AI 构建提示词）
std::string DBBusiness::getTableStruct(const std::string& connectionId,
                                       const std::string& tableName) {
    auto db = getDatabase(connectionId);
    if (!db) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::DB_CONNECTION_NOT_EXISTS);
    }
    auto columnInfos = db->getTableStruct(tableName);
    std::ostringstream oss;
    oss << "Table: " << tableName << "\n";
    for (const auto& col : columnInfos) {
        oss << col.name << " " << col.type;
        if (col.primaryKey) oss << " PRIMARY KEY";
        if (!col.nullable) oss << " NOT NULL";
        if (!col.defaultValue.empty()) oss << " DEFAULT " << col.defaultValue;
        if (col.autoIncrement) oss << " AUTO_INCREMENT";
        oss << "\n";
    }
    return oss.str();
}

// 获取表采样数据（制表符分隔，供 AI 构建提示词）
std::string DBBusiness::getSampleData(const std::string& connectionId,
                                      const std::string& tableName, int32_t limit) {
    auto db = getDatabase(connectionId);
    if (!db) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::DB_CONNECTION_NOT_EXISTS);
    }
    // 采样条数：<=0 归 5
    int32_t sampleLimit = (limit > 0) ? limit : 5;
    std::string sql = "SELECT * FROM " + db->quoteIdentifier(tableName) +
                      " LIMIT " + std::to_string(sampleLimit);
    auto result = db->executeQuery(sql);
    if (!result->success()) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::DB_SAMPLE_DATA_GET_FAILED);
    }
    std::ostringstream oss;
    bool firstRow = true;
    for (const auto& row : result->_rows) {
        if (!firstRow) oss << "\n";
        firstRow = false;
        for (size_t i = 0; i < row.size(); ++i) {
            if (i > 0) oss << "\t";
            oss << row[i];
        }
    }
    INF("sample data: {}", oss.str());
    return oss.str();
}

// 单元格值 → 预编译参数（按目标列类型转换）
// 课件问题㊼：替代课件的"手工拼 SQL + 只转义单引号"，值永不进 SQL 文本
static PreparedParam buildCellParam(const std::string& value, const std::string& colType) {
    // 空值 / NULL 标记 → SQL NULL
    if (value.empty() || value == "NULL" || value == "N/A" || value == "n/a") {
        return PreparedParam(nullptr);
    }
    if (colType == "BIGINT" || colType == "INTEGER" || colType == "INT") {
        try { return PreparedParam(static_cast<long long>(std::stoll(value))); }
        catch (...) { return PreparedParam(nullptr); }   // 脏数据按 NULL 落库，不中断整批
    }
    if (colType == "DOUBLE" || colType == "FLOAT" || colType == "REAL") {
        try { return PreparedParam(std::stod(value)); }
        catch (...) { return PreparedParam(nullptr); }
    }
    if (colType == "BOOLEAN") {
        return PreparedParam(value == "1" || value == "true" || value == "TRUE" ||
                             value == "yes" || value == "y");
    }
    return PreparedParam(value);   // TEXT / DATE 等按字符串绑定
}

// 导入 Excel worksheet 数据到指定表
chat2Data::DatabaseService::ImportExcelDataResult DBBusiness::importExcelData(
    const std::string& connectionId, const std::string& tableName,
    const WorksheetData& worksheetData) {
    auto db = getDatabase(connectionId);
    if (!db) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::DB_CONNECTION_NOT_EXISTS);
    }
    // 1. 建表
    if (!createTableForWorksheet(connectionId, tableName, worksheetData)) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::DB_IMPORT_DATA_FAILED);
    }
    // 2. 导入数据
    if (!importWorksheetData(connectionId, tableName, worksheetData)) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::DB_IMPORT_DATA_FAILED);
    }
    // 3. 组装返回
    chat2Data::DatabaseService::ImportExcelDataResult result;
    int rowCount = worksheetData.rows_size();
    result.set_table_name(tableName);
    result.set_imported_rows(rowCount);
    INF("Excel data imported: tableName={}, rows={}", tableName, rowCount);
    return result;
}

// 创建 worksheet 对应的数据库表
// 课件问题㊺修复：课件统一生成 "id INTEGER PRIMARY KEY AUTO_INCREMENT"——
//   AUTO_INCREMENT 是 MySQL 专有语法，SQLite 建表直接失败。
//   按数据库类型分支：MySQL 用 BIGINT AUTO_INCREMENT，SQLite 用 INTEGER PRIMARY KEY AUTOINCREMENT
bool DBBusiness::createTableForWorksheet(const std::string& connectionId,
                                         const std::string& tableName,
                                         const WorksheetData& worksheetData) {
    auto db = getDatabase(connectionId);
    if (!db) {
        return false;
    }
    // 1. 主键列按数据库类型选择语法
    std::string idColumn;
    if (db->getDatabaseType() == DBType::MYSQL) {
        idColumn = "`id` BIGINT AUTO_INCREMENT PRIMARY KEY";
    } else {
        idColumn = "\"id\" INTEGER PRIMARY KEY AUTOINCREMENT";
    }
    // 2. 拼建表语句（列类型经驱动层转换：SQLite 的 BOOLEAN→INTEGER、DATE→TEXT）
    std::ostringstream createSql;
    createSql << "CREATE TABLE IF NOT EXISTS " << db->quoteIdentifier(tableName)
              << " (" << idColumn;
    for (const auto& col : worksheetData.columns()) {
        std::string colType = db->convertExcelTypeToSql(col.type());
        createSql << ", " << db->quoteIdentifier(col.name()) << " " << colType;
    }
    createSql << ")";
    // 3. MySQL 追加表选项（SQLite 不支持）
    if (db->getDatabaseType() == DBType::MYSQL) {
        createSql << " ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";
    }
    INF("Create table SQL: {}", createSql.str());
    // 4. 执行建表
    auto result = db->executeModify(createSql.str());
    if (!result->success()) {
        ERR("Failed to create table: {}, error: {}", tableName, result->_errorMsg);
        return false;
    }
    INF("Table created: {}", tableName);
    return true;
}

// 导入 worksheet 数据（㊼ 参数化批量插入：值不参与 SQL 拼接，无注入面）
bool DBBusiness::importWorksheetData(const std::string& connectionId,
                                     const std::string& tableName,
                                     const WorksheetData& worksheetData) {
    auto db = getDatabase(connectionId);
    if (!db) {
        return false;
    }
    const int colCount = worksheetData.columns_size();
    const int rowCount = worksheetData.rows_size();
    if (colCount == 0) {
        return false;
    }
    // 1. 列类型（驱动层转换后的实际库内类型）
    std::vector<std::string> colTypes;
    colTypes.reserve(colCount);
    for (const auto& col : worksheetData.columns()) {
        colTypes.push_back(db->convertExcelTypeToSql(col.type()));
    }
    // 2. 列名列表（拼一次复用）
    std::ostringstream colsSql;
    for (int c = 0; c < colCount; ++c) {
        if (c > 0) colsSql << ", ";
        colsSql << db->quoteIdentifier(worksheetData.columns(c).name());
    }
    // 3. 分批插入：每批最多 BATCH_SIZE 行，占位符 + 参数数组
    for (int start = 0; start < rowCount; start += BATCH_SIZE) {
        const int end = std::min(start + BATCH_SIZE, rowCount);
        std::ostringstream insertSql;
        insertSql << "INSERT INTO " << db->quoteIdentifier(tableName)
                  << " (" << colsSql.str() << ") VALUES ";
        std::vector<PreparedParam> params;
        params.reserve(static_cast<size_t>(end - start) * colCount);
        for (int r = start; r < end; ++r) {
            if (r > start) insertSql << ", ";
            insertSql << "(";
            const auto& row = worksheetData.rows(r);
            for (int c = 0; c < colCount; ++c) {
                if (c > 0) insertSql << ", ";
                insertSql << "?";
                std::string value = (c < row.cells_size()) ? row.cells(c).value() : "";
                params.push_back(buildCellParam(value, colTypes[c]));
            }
            insertSql << ")";
        }
        // 4. 参数化执行
        auto result = db->executePreparedModify(insertSql.str(), params);
        if (!result->success()) {
            ERR("Failed to insert batch at row {}: {}", start, result->_errorMsg);
            return false;
        }
    }
    INF("Data imported to table: {}, rows: {}", tableName, rowCount);
    return true;
}

// 删除 Excel 文件对应的数据库表（含其临时表）
chat2Data::DatabaseService::DropTableExcelResult DBBusiness::dropTableExcel(
    const std::string& connectionId, const std::vector<std::string>& tableNames) {
    auto db = getDatabase(connectionId);
    if (!db) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::DB_CONNECTION_NOT_EXISTS);
    }
    chat2Data::DatabaseService::DropTableExcelResult result;
    for (const auto& tableName : tableNames) {
        // 1. 删除原表
        std::string dropSql = "DROP TABLE IF EXISTS " + db->quoteIdentifier(tableName);
        auto dropResult = db->executeModify(dropSql);
        if (!dropResult->success()) {
            result.add_failed_tables(tableName);
            WRN("Failed to drop table: {}, error: {}", tableName, dropResult->_errorMsg);
            continue;
        }
        result.add_dropped_tables(tableName);
        // 2. 连带删除该表的临时表（先取名字再删，避免持锁做 DB IO）
        std::string tempTable;
        {
            std::lock_guard<std::mutex> lock(_tempTableMapMutex);
            auto connIt = _tempTableMap.find(connectionId);
            if (connIt != _tempTableMap.end()) {
                for (const auto& backup : connIt->second) {
                    if (backup.originalTable == tableName) {
                        tempTable = backup.tempTable;
                        break;
                    }
                }
            }
        }
        if (!tempTable.empty()) {
            deleteTempTable(connectionId, tempTable);
        }
        // 3. 清理映射
        {
            std::lock_guard<std::mutex> lock(_tempTableMapMutex);
            auto connIt = _tempTableMap.find(connectionId);
            if (connIt != _tempTableMap.end()) {
                auto& backups = connIt->second;
                backups.erase(std::remove_if(backups.begin(), backups.end(),
                                             [&tableName](const TempTableBackup& b) {
                                                 return b.originalTable == tableName;
                                             }),
                              backups.end());
            }
        }
    }
    result.set_dropped_count(result.dropped_tables_size());
    return result;
}

// 删除指定用户创建的所有连接（连带其临时表）
bool DBBusiness::deleteUserAllConn(const std::string& userId) {
    // 1. 取用户的所有连接ID
    auto connIds = _connMgr->getUserConnectionIds(userId);
    // 2. 逐个连接清理临时表
    for (const auto& connId : connIds) {
        auto tempTables = getConnTempTables(connId);
        for (const auto& tableName : tempTables) {
            deleteTempTable(connId, tableName);
        }
    }
    // 3. 删除用户所有连接
    _connMgr->deleteUserAllConnections(userId);
    INF("User all connections deleted with temp tables: userId={}, connCount={}",
        userId, connIds.size());
    return true;
}

} // namespace databaseService

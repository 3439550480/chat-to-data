#pragma once
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <mutex>
#include <unordered_map>
#include <gflags/gflags.h>
#include <bite_scaffold/rpc.h>
#include "dbConnMgr.h"
#include "dbDriver/databaseSchema.h"
#include "../proto/protoCode/dbService.pb.h"

// 调用文件子服务的 gflags key（与 main.cc 的 DEFINE 配对；file service 注册名为 FileService）
DECLARE_string(file_service);

namespace databaseService {

// 表数据查询结果（业务层 → RPC 层的载体）
struct TableDataResult {
    std::vector<std::string> columns;             // 列名集合
    std::vector<std::string> columnTypes;         // 列类型集合
    std::vector<std::vector<std::string>> rows;   // 行数据集合
    int totalRows = 0;                            // 总行数
    int currentPage = 1;                          // 当前页码
    int totalPages = 1;                           // 总页数
    int pageSize = 50;                            // 每页行数
};

// 原表 → 临时表 的备份映射
struct TempTableBackup {
    std::string originalTable;   // 原表名
    std::string tempTable;       // 临时表名
    int64_t backupTime;          // 备份时间戳（毫秒）
};

class DBBusiness {
public:
    DBBusiness(std::shared_ptr<DBConnMgr> connMgr,
               std::shared_ptr<biterpc::SvcChannels> svcChannels,
               MySQLConfig defaultMySQLConfig);
    // ---------- 连接生命周期 ----------
    // 新建数据库连接（MySQL 直连 / SQLite 经文件服务取文件后下载到本地）
    // @throws DB_PARAM_INVALID / DB_CONNECTION_FAILED / DB_SQLITE_DOWNLOAD_FAILED
    std::string connectDatabase(const std::string& userId,
                                const chat2Data::DatabaseService::DatabaseConfig& config);
    // 断开数据库连接（清理 SQLite 本地文件 + 该连接临时表 + 移除连接）
    bool disconnectDatabase(const std::string& connectionId);
    // ---------- 元数据与数据 ----------
    // 获取数据库表列表
    // @throws DB_CONNECTION_NOT_EXISTS
    std::vector<std::string> listTables(const std::string& connectionId);
    // 获取指定表的数据（默认读临时表；forceOriginal=true 强制读原表）
    // @throws DB_CONNECTION_NOT_EXISTS / DB_TABLE_DATA_GET_FAILED
    TableDataResult getTableData(const std::string& connectionId,
                                 const std::string& tableName,
                                 bool forceOriginal,
                                 int32_t pageNumber,
                                 int32_t pageSize);
    // 执行SQL语句（校验 → 修改类走临时表沙箱 → 执行）
    chat2Data::DatabaseService::ExecuteSQLResponse executeSQL(const std::string& connectionId,
                                                              const std::string& sql);
    // 获取表结构（格式化字符串，供 AI 构建提示词）
    // @throws DB_CONNECTION_NOT_EXISTS
    std::string getTableStruct(const std::string& connectionId, const std::string& tableName);
    // 获取表采样数据（制表符分隔字符串，供 AI 构建提示词）
    // @throws DB_CONNECTION_NOT_EXISTS / DB_SAMPLE_DATA_GET_FAILED
    std::string getSampleData(const std::string& connectionId,
                              const std::string& tableName, int32_t limit);
    // ---------- Excel 数据导入/删除 ----------
    using WorksheetData = chat2Data::excelParseService::WorksheetData;
    // 导入 Excel worksheet 数据到指定表（建表 + 分批插入）
    // @throws DB_CONNECTION_NOT_EXISTS / DB_IMPORT_DATA_FAILED
    chat2Data::DatabaseService::ImportExcelDataResult importExcelData(
        const std::string& connectionId, const std::string& tableName,
        const WorksheetData& worksheetData);
    // 删除 Excel 文件对应的数据库表（含其临时表）
    // @throws DB_CONNECTION_NOT_EXISTS
    chat2Data::DatabaseService::DropTableExcelResult dropTableExcel(
        const std::string& connectionId, const std::vector<std::string>& tableNames);
    // 获取指定连接下的所有临时表
    std::vector<std::string> getConnTempTables(const std::string& connectionId);
    // 删除指定用户创建的所有连接（连带其临时表）
    bool deleteUserAllConn(const std::string& userId);
private:
    // 按连接ID取数据库实例（不存在返回 nullptr）
    std::shared_ptr<IDatabase> getDatabase(const std::string& connectionId);
    // 备份表（生成临时表 + 记录映射）
    std::string backupTable(const std::string& connectionId, const std::string& tableName);
    // 删除指定临时表
    bool deleteTempTable(const std::string& connectionId, const std::string& tempTable);
    // 删除指定连接下所有临时表（含映射清理）
    bool deleteTempTablesForConnection(const std::string& connectionId);
    // 将 SQL 中的原表名替换为临时表名（㊻：带词边界判断）
    std::string replaceTableNameWithTemp(const std::string& connectionId,
                                         const std::string& sql,
                                         const std::string& originalTable);
    // 构建临时表名
    std::string buildTempTableName(const std::string& originalTable);
    // 创建 worksheet 对应的数据库表（㊺：按数据库类型分支生成主键语法）
    bool createTableForWorksheet(const std::string& connectionId,
                                 const std::string& tableName,
                                 const WorksheetData& worksheetData);
    // 导入 worksheet 数据（㊼：参数化批量插入）
    bool importWorksheetData(const std::string& connectionId,
                             const std::string& tableName,
                             const WorksheetData& worksheetData);
    // 从文件子服务获取 SQLite 文件并下载到本地
    // @throws DB_CONNECTION_FAILED / DB_SQLITE_DOWNLOAD_FAILED
    std::string downloadSQLiteFile(const std::string& fileId);
private:
    std::shared_ptr<DBConnMgr> _connMgr;                          // 连接管理器
    std::shared_ptr<biterpc::SvcChannels> _svcChannels;           // 跨服务通道
    std::map<std::string, std::vector<TempTableBackup>> _tempTableMap;  // connId → 临时表备份
    std::mutex _tempTableMapMutex;                                // 保护 _tempTableMap
    const static int BATCH_SIZE = 100;                            // 批量插入批次大小
};

} // namespace databaseService

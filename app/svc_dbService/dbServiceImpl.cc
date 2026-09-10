#include <bite_scaffold/log.h>
#include "dbServiceImpl.h"
#include "dbBusiness.h"
#include "../common/errorHandler.h"

namespace databaseService {

DBServiceImpl::DBServiceImpl(std::shared_ptr<DBBusiness> dbBusiness)
    : _dbBusiness(dbBusiness) {
    INF("DBServiceImpl initialized");
}

DBServiceImpl::~DBServiceImpl() {}

// 连接数据库（oneof 配置原样透传给业务层，由业务层按 type 分支）
void DBServiceImpl::ConnectDatabase(
    ::google::protobuf::RpcController* controller,
    const chat2Data::DatabaseService::ConnectDatabaseRequest* request,
    chat2Data::DatabaseService::ConnectDatabaseResponse* response,
    ::google::protobuf::Closure* done) {
    // 1. ClosureGuard 以 RAII 机制管理 done->Run()
    brpc::ClosureGuard done_guard(done);
    // 2. 解析请求参数
    std::string requestId = request->request_id();
    std::string sessionId = request->session_id();
    std::string userId = request->user_id();
    const auto& config = request->database();
    // 3. 校验参数
    if (userId.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::DB_PARAM_INVALID));
        response->set_error_msg("User ID is empty");
        return;
    }
    try {
        // 4. 新建连接（业务层内部：MySQL 直连 / SQLite 下载文件后连接）
        std::string connectionId = _dbBusiness->connectDatabase(userId, config);
        // 5. 构建响应
        response->set_request_id(requestId);
        response->set_error_code(0);
        response->mutable_result()->set_connection_id(connectionId);
        INF("ConnectDatabase success: requestId={}, connId={}", requestId, connectionId);
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

// 断开数据库连接
void DBServiceImpl::DisconnectDatabase(
    ::google::protobuf::RpcController* controller,
    const chat2Data::DatabaseService::DisconnectDatabaseRequest* request,
    chat2Data::DatabaseService::DisconnectDatabaseResponse* response,
    ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string connectionId = request->connection_id();
    if (connectionId.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::DB_PARAM_INVALID));
        response->set_error_msg("Connection ID is empty");
        return;
    }
    try {
        // 业务层内部顺序：清 SQLite 本地文件 → 清临时表 → 交 ConnMgr 移除
        //（登记项 ㊿：返回值未使用，不存在的连接会被报告为"断开成功"，与课件一致）
        _dbBusiness->disconnectDatabase(connectionId);
        response->set_request_id(requestId);
        response->set_error_code(0);
        INF("DisconnectDatabase success: requestId={}, connId={}", requestId, connectionId);
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

// 列出数据库中的所有表
void DBServiceImpl::ListTables(
    ::google::protobuf::RpcController* controller,
    const chat2Data::DatabaseService::ListTablesRequest* request,
    chat2Data::DatabaseService::ListTablesResponse* response,
    ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string connectionId = request->db_connect_id();
    if (connectionId.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::DB_PARAM_INVALID));
        response->set_error_msg("Connection ID is empty");
        return;
    }
    try {
        auto tables = _dbBusiness->listTables(connectionId);
        response->set_request_id(requestId);
        response->set_error_code(0);
        for (const auto& table : tables) {
            response->mutable_result()->add_tables(table);
        }
        INF("ListTables success: requestId={}, connId={}, tableCount={}",
            requestId, connectionId, tables.size());
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

// 获取表中的数据（分页 + 临时表优先）
void DBServiceImpl::GetTableData(
    ::google::protobuf::RpcController* controller,
    const chat2Data::DatabaseService::GetTableDataRequest* request,
    chat2Data::DatabaseService::GetTableDataResponse* response,
    ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string connectionId = request->db_connect_id();
    std::string tableName = request->table_name();
    bool forceOriginal = request->force_original();
    int32_t pageNumber = request->page_number();
    int32_t pageSize = request->page_size();
    if (connectionId.empty() || tableName.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::DB_PARAM_INVALID));
        response->set_error_msg("Connection ID or table name is empty");
        return;
    }
    try {
        auto tableResult = _dbBusiness->getTableData(
            connectionId, tableName, forceOriginal, pageNumber, pageSize);
        response->set_request_id(requestId);
        response->set_error_code(0);
        // 列信息
        auto* schemaInfo = response->mutable_result()->mutable_table_schema();
        for (size_t i = 0; i < tableResult.columns.size(); ++i) {
            auto* colInfo = schemaInfo->add_column_info();
            colInfo->set_name(tableResult.columns[i]);
            if (i < tableResult.columnTypes.size()) {
                colInfo->set_type(tableResult.columnTypes[i]);
            }
        }
        // 分页元信息
        auto* tableData = schemaInfo->mutable_table_data();
        tableData->set_total_rows(tableResult.totalRows);
        tableData->set_current_page(tableResult.currentPage);
        tableData->set_total_pages(tableResult.totalPages);
        tableData->set_page_size(tableResult.pageSize);
        // 行数据
        for (const auto& row : tableResult.rows) {
            auto* protoRow = tableData->add_rows();
            for (const auto& cell : row) {
                protoRow->add_cells(cell);
            }
        }
        INF("GetTableData success: requestId={}, connId={}, table={}, rows={}",
            requestId, connectionId, tableName, tableResult.rows.size());
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

// 执行 SQL（结果集整体透传：查询返回列/类型/行，修改返回影响行数）
void DBServiceImpl::ExecuteSQL(
    ::google::protobuf::RpcController* controller,
    const chat2Data::DatabaseService::ExecuteSQLRequest* request,
    chat2Data::DatabaseService::ExecuteSQLResponse* response,
    ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string connectionId = request->db_connect_id();
    std::string sql = request->sql();
    if (connectionId.empty() || sql.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::DB_PARAM_INVALID));
        response->set_error_msg("Connection ID or SQL is empty");
        return;
    }
    try {
        // 业务层已组装完整响应（含 error_code/is_query/columns/rows/affected_rows）
        auto sqlResponse = _dbBusiness->executeSQL(connectionId, sql);
        response->set_request_id(requestId);
        response->set_error_code(sqlResponse.error_code());
        response->set_error_msg(sqlResponse.error_msg());
        response->set_is_query(sqlResponse.is_query());
        response->set_affected_rows(sqlResponse.affected_rows());
        for (int i = 0; i < sqlResponse.columns_size(); ++i) {
            response->add_columns(sqlResponse.columns(i));
        }
        for (int i = 0; i < sqlResponse.column_types_size(); ++i) {
            response->add_column_types(sqlResponse.column_types(i));
        }
        for (int i = 0; i < sqlResponse.rows_size(); ++i) {
            *response->add_rows() = sqlResponse.rows(i);   // 嵌套 message 整体拷贝
        }
        INF("ExecuteSQL finished: requestId={}, connId={}, isQuery={}",
            requestId, connectionId, sqlResponse.is_query());
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

// 获取数据库连接中的临时表
void DBServiceImpl::GetConnTempTables(
    ::google::protobuf::RpcController* controller,
    const chat2Data::DatabaseService::GetConnTempTablesRequest* request,
    chat2Data::DatabaseService::GetConnTempTablesResponse* response,
    ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string connectionId = request->db_connect_id();
    if (connectionId.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::DB_PARAM_INVALID));
        response->set_error_msg("Connection ID is empty");
        return;
    }
    try {
        auto tempTables = _dbBusiness->getConnTempTables(connectionId);
        response->set_request_id(requestId);
        response->set_error_code(0);
        response->set_has_temp_tables(!tempTables.empty());
        for (const auto& table : tempTables) {
            response->add_temp_tables(table);
        }
        INF("GetConnTempTables success: requestId={}, connId={}, count={}",
            requestId, connectionId, tempTables.size());
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

// 获取表的结构
void DBServiceImpl::GetTableStruct(
    ::google::protobuf::RpcController* controller,
    const chat2Data::DatabaseService::GetTableStructRequest* request,
    chat2Data::DatabaseService::GetTableStructResponse* response,
    ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string connectionId = request->db_connect_id();
    std::string tableName = request->table_name();
    if (connectionId.empty() || tableName.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::DB_PARAM_INVALID));
        response->set_error_msg("Connection ID or table name is empty");
        return;
    }
    try {
        auto tableStruct = _dbBusiness->getTableStruct(connectionId, tableName);
        response->set_request_id(requestId);
        response->set_error_code(0);
        response->set_table_struct(tableStruct);
        INF("GetTableStruct success: requestId={}, connId={}, table={}",
            requestId, connectionId, tableName);
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

// 获取表的采样数据
void DBServiceImpl::GetSampleData(
    ::google::protobuf::RpcController* controller,
    const chat2Data::DatabaseService::GetSampleDataRequest* request,
    chat2Data::DatabaseService::GetSampleDataResponse* response,
    ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string connectionId = request->db_connect_id();
    std::string tableName = request->table_name();
    int32_t limit = request->limit();
    if (connectionId.empty() || tableName.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::DB_PARAM_INVALID));
        response->set_error_msg("Connection ID or table name is empty");
        return;
    }
    try {
        auto sampleData = _dbBusiness->getSampleData(connectionId, tableName, limit);
        response->set_request_id(requestId);
        response->set_error_code(0);
        response->set_sample_data(sampleData);
        INF("GetSampleData success: requestId={}, connId={}, table={}",
            requestId, connectionId, tableName);
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

// 导入 Excel 数据到数据库
void DBServiceImpl::ImportExcelData(
    ::google::protobuf::RpcController* controller,
    const chat2Data::DatabaseService::ImportExcelDataRequest* request,
    chat2Data::DatabaseService::ImportExcelDataResponse* response,
    ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string connectionId = request->db_connect_id();
    std::string tableName = request->table_name();
    const auto& worksheetData = request->worksheet_data();
    if (connectionId.empty() || tableName.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::DB_PARAM_INVALID));
        response->set_error_msg("Connection ID or table name is empty");
        return;
    }
    try {
        auto importResult = _dbBusiness->importExcelData(connectionId, tableName, worksheetData);
        response->set_request_id(requestId);
        response->set_error_code(0);
        response->mutable_result()->set_table_name(importResult.table_name());
        response->mutable_result()->set_imported_rows(importResult.imported_rows());
        INF("ImportExcelData success: requestId={}, connId={}, table={}, rows={}",
            requestId, connectionId, tableName, importResult.imported_rows());
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

// 删除 Excel 文件对应的数据库表
void DBServiceImpl::DropTableExcel(
    ::google::protobuf::RpcController* controller,
    const chat2Data::DatabaseService::DropTableExcelRequest* request,
    chat2Data::DatabaseService::DropTableExcelResponse* response,
    ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string connectionId = request->db_connect_id();
    std::vector<std::string> tableNames(request->table_names().begin(),
                                        request->table_names().end());
    if (connectionId.empty() || tableNames.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::DB_PARAM_INVALID));
        response->set_error_msg("Connection ID or table names are empty");
        return;
    }
    try {
        auto dropResult = _dbBusiness->dropTableExcel(connectionId, tableNames);
        response->set_request_id(requestId);
        response->set_error_code(0);
        response->mutable_result()->set_dropped_count(dropResult.dropped_count());
        for (const auto& table : dropResult.dropped_tables()) {
            response->mutable_result()->add_dropped_tables(table);
        }
        for (const auto& table : dropResult.failed_tables()) {
            response->mutable_result()->add_failed_tables(table);
        }
        INF("DropTableExcel success: requestId={}, connId={}, dropped={}",
            requestId, connectionId, dropResult.dropped_count());
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

// 删除用户的所有数据库连接
void DBServiceImpl::DeleteUserAllConn(
    ::google::protobuf::RpcController* controller,
    const chat2Data::DatabaseService::DeleteUserAllConnRequest* request,
    chat2Data::DatabaseService::DeleteUserAllConnResponse* response,
    ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string userId = request->user_id();
    if (userId.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::DB_PARAM_INVALID));
        response->set_error_msg("User ID is empty");
        return;
    }
    try {
        _dbBusiness->deleteUserAllConn(userId);
        response->set_request_id(requestId);
        response->set_error_code(0);
        INF("DeleteUserAllConn success: requestId={}, userId={}", requestId, userId);
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

} // namespace databaseService

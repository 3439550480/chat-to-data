#include <sstream>
#include <ctime>
#include <algorithm>
#include <filesystem>
#include <jsoncpp/json/value.h>
#include <jsoncpp/json/reader.h>
#include <jsoncpp/json/writer.h>
#include <sqlite3.h>
#include <bite_scaffold/log.h>
#include <bite_scaffold/util.h>
#include "aiMessageHandler.h"
#include "chatSessionMgr.h"
#include "promptTemplate.h"
#include "userPrompt.h"
#include "../common/errorHandler.h"
#include "../common/utils.h"
#include "../common/sqlValidator.h"

namespace aiService {

AIMessageHandler::AIMessageHandler(std::shared_ptr<ai_chat_sdk::ChatSDK> chatSdk,
                                   std::shared_ptr<ChatSessionMgr> chatSessionMgr,
                                   std::shared_ptr<biterpc::SvcChannels> svcChannels)
    : _chatSdk(chatSdk), _chatSessionMgr(chatSessionMgr), _svcChannels(svcChannels) {
    INF("AIMessageHandler initialized");
}

// ---------- 推送 ----------

// 推送消息给前端（writeChunk 为空时静默——邮件流程的模型输出就走这条路）
void AIMessageHandler::pushMessage(const std::string& content, bool done,
                                   SendMessageCallback writeChunk) {
    if (writeChunk) {
        writeChunk(content, done);
    }
}

// ---------- 模型交互 ----------

// 发送消息给模型：流式回调里累积完整响应 + 实时推流
// 注意：最终的 done 信号由主流程统一发送（分析/总结阶段之间不能发 done）
std::string AIMessageHandler::sendMessageToModel(const std::string& chatSessionId,
                                                 const std::string& message,
                                                 SendMessageCallback writeChunk) {
    std::string fullResponse;
    _chatSdk->sendMessageStream(chatSessionId, message,
        [&fullResponse, writeChunk](const std::string& chunk, bool done) {
            fullResponse += chunk;
            if (writeChunk) {
                writeChunk(chunk, false);
            }
            return true;                     // true = 继续接收下一块
        });
    return fullResponse;
}

// ---------- 标签提取与 SQL 处理 ----------

// 提取 <tagStart>...<tagEnd> 之间的内容（提示词输出协议的解析基础）
std::string AIMessageHandler::extractTagContent(const std::string& response,
                                                const std::string& tagStart,
                                                const std::string& tagEnd) {
    // 1. 查找起始标签
    size_t startPos = response.find(tagStart);
    if (startPos == std::string::npos) {
        return "";
    }
    // 2. 查找结束标签
    startPos += tagStart.length();
    size_t endPos = response.find(tagEnd, startPos);
    if (endPos == std::string::npos) {
        return "";
    }
    // 3. 提取内容
    return response.substr(startPos, endPos - startPos);
}

// 从模型响应中提取 SQL 语句
std::string AIMessageHandler::extractSql(const std::string& response) {
    return extractTagContent(response, "<SQL_START>", "<SQL_END>");
}

// SQL 规范化 + 三关校验（复用 16 章 SQLValidator：危险词/多语句/类型白名单）
// 校验不过抛异常——模型生成的 SQL 同样不许越权
std::string AIMessageHandler::normalizeSql(const std::string& sql) {
    std::string normalizedSql = chat2Data::SQLValidator::normalize(sql);
    if (chat2Data::SQLValidator::validate(normalizedSql)) {
        return normalizedSql;
    }
    throw chat2Data::Chat2DataException(chat2Data::ErrorCode::AI_SEND_MESSAGE_FAILED);
}

// ---------- SQL 执行 ----------

// 经数据库子服务执行 SQL
// 17 章的沙箱能力在此自动生效：修改类 SQL 会被备份到临时表执行，原表零改动
SqlExecuteResult AIMessageHandler::executeSql(const std::string& sessionId,
                                              const std::string& dbConnectId,
                                              const std::string& sql) {
    SqlExecuteResult result;
    result._success = false;
    result._isQuery = false;
    result._affectedRows = 0;
    // 1. 取数据库子服务信道
    auto channel = _svcChannels->getNode(FLAGS_db_service);
    if (!channel) {
        ERR("Failed to get DatabaseService channel");
        result._errorMsg = "Database service unavailable";
        return result;
    }
    // 2. 构建 RPC 请求
    chat2Data::DatabaseService::ExecuteSQLRequest rpcRequest;
    rpcRequest.set_request_id(chat2Data::Utils::generateUuid());
    rpcRequest.set_session_id(sessionId);
    rpcRequest.set_db_connect_id(dbConnectId);
    rpcRequest.set_sql(sql);
    // 3. 发起 RPC 调用
    chat2Data::DatabaseService::DatabaseService_Stub stub(channel.get());
    chat2Data::DatabaseService::ExecuteSQLResponse rpcResponse;
    brpc::Controller controller;
    stub.ExecuteSQL(&controller, &rpcRequest, &rpcResponse, nullptr);
    // 4. 检测结果
    if (controller.Failed()) {
        ERR("ExecuteSQL RPC failed: {}", controller.ErrorText());
        result._errorMsg = controller.ErrorText();
        return result;
    }
    if (rpcResponse.error_code() != 0) {
        ERR("ExecuteSQL failed: errorCode={}, errorMsg={}",
            rpcResponse.error_code(), rpcResponse.error_msg());
        result._errorMsg = rpcResponse.error_msg();
        return result;
    }
    // 5. 解析执行结果
    result._success = true;
    result._isQuery = rpcResponse.is_query();
    result._affectedRows = rpcResponse.affected_rows();
    for (const auto& col : rpcResponse.columns()) {
        result._columns.push_back(col);
    }
    for (const auto& colType : rpcResponse.column_types()) {
        result._columnTypes.push_back(colType);
    }
    for (const auto& row : rpcResponse.rows()) {
        std::vector<std::string> rowData;
        for (const auto& cell : row.cells()) {
            rowData.push_back(cell);
        }
        result._rows.push_back(rowData);
    }
    INF("SQL执行成功: dbConnectId={}, columns={}, rows={}",
        dbConnectId, result._columns.size(), result._rows.size());
    return result;
}

// ---------- 结果组装 ----------

// SQL 执行结果 → JSON（喂给总结提示词；失败时给 error 字段让模型向用户解释）
std::string AIMessageHandler::buildSqlResultJson(const SqlExecuteResult& sqlResult) {
    Json::Value resultJson;
    if (sqlResult._success) {
        // 1. 列名称
        Json::Value columnsJson(Json::arrayValue);
        for (const auto& col : sqlResult._columns) {
            columnsJson.append(col);
        }
        resultJson["columns"] = columnsJson;
        // 2. 行数据
        Json::Value rowsJson(Json::arrayValue);
        for (const auto& row : sqlResult._rows) {
            Json::Value rowJson(Json::arrayValue);
            for (const auto& cell : row) {
                rowJson.append(cell);
            }
            rowsJson.append(rowJson);
        }
        resultJson["rows"] = rowsJson;
    } else {
        resultJson["error"] = sqlResult._errorMsg;
    }
    // 3. 转 JSON 字符串
    Json::StreamWriterBuilder builder;
    return Json::writeString(builder, resultJson);
}

// 从总结消息（纯 JSON）中提取 summary 字段
std::string AIMessageHandler::getSummary(const std::string& summaryMessage) {
    auto summaryJsonOpt = biteutil::JSON::unserialize(summaryMessage);
    if (!summaryJsonOpt.has_value() || !summaryJsonOpt.value().isObject() ||
        !summaryJsonOpt.value().isMember("summary")) {
        ERR("Failed to parse summary message, summaryMessage={}", summaryMessage);
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::AI_SEND_MESSAGE_FAILED);
    }
    return summaryJsonOpt.value()["summary"].asString();
}

// 从总结消息中提取图表类型
std::string AIMessageHandler::getDisplayType(const std::string& summaryMessage) {
    auto summaryJsonOpt = biteutil::JSON::unserialize(summaryMessage);
    if (!summaryJsonOpt.has_value() || !summaryJsonOpt.value().isObject()) {
        ERR("Failed to parse summary message, summaryMessage={}", summaryMessage);
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::AI_SEND_MESSAGE_FAILED);
    }
    if (summaryJsonOpt.value().isMember("chartType")) {
        return summaryJsonOpt.value()["chartType"].asString();
    }
    return "";
}

// 最终响应 = 模型总结 JSON（保留 taskStatus/keyFindings/summary/chartType/chartConfig）
//            + data（SQL 执行结果，前端图表渲染用）
// 单行序列化：SSE 以 \n\n 分块，JSON 内换行会破坏前端按块解析
std::string AIMessageHandler::buildFinalResponse(const std::string& summaryResponseJson,
                                                 const std::string& displayType,
                                                 const SqlExecuteResult& sqlResult) {
    auto responseJsonOpt = biteutil::JSON::unserialize(summaryResponseJson);
    if (!responseJsonOpt.has_value()) {
        ERR("Failed to parse summary response json");
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::AI_SEND_MESSAGE_FAILED);
    }
    Json::Value responseJson = responseJsonOpt.value();
    // data 字段：SQL 执行结果
    Json::Value dataJson;
    if (sqlResult._success) {
        Json::Value columnsJson(Json::arrayValue);
        for (const auto& col : sqlResult._columns) {
            columnsJson.append(col);
        }
        dataJson["columns"] = columnsJson;

        Json::Value columnTypesJson(Json::arrayValue);
        for (const auto& colType : sqlResult._columnTypes) {
            columnTypesJson.append(colType);
        }
        dataJson["columnTypes"] = columnTypesJson;

        Json::Value rowsJson(Json::arrayValue);
        for (const auto& row : sqlResult._rows) {
            Json::Value rowJson(Json::arrayValue);
            for (const auto& cell : row) {
                rowJson.append(cell);
            }
            rowsJson.append(rowJson);
        }
        dataJson["rows"] = rowsJson;
    }
    responseJson["data"] = dataJson;
    // 单行序列化（避免 SSE 传输时被换行符分割）
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, responseJson);
}

// ---------- 场景数据获取 ----------

// 数据库类型枚举 → 提示词字符串
std::string AIMessageHandler::getDatabaseTypeString(chat2Data::AiService::DataType dbType) {
    switch (dbType) {
        case chat2Data::AiService::DataType::EXCEL:
            return "Excel";
        case chat2Data::AiService::DataType::MYSQL:
            return "MySQL";
        case chat2Data::AiService::DataType::SQLITE:
            return "SQLite";
        default:
            return "Unknown";
    }
}

// 数据库场景：请求参数中的表名串拆分（多个表名以逗号分隔）
std::vector<std::string> AIMessageHandler::getDatabaseTables(const std::string& tableNames) {
    std::vector<std::string> result;
    if (tableNames.empty()) {
        return result;
    }
    std::stringstream ss(tableNames);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) {
            result.push_back(item);
        }
    }
    return result;
}

// 表名列表总入口：excel 经文件子服务；database 解析请求参数
std::vector<std::string> AIMessageHandler::getDatabaseTables(const SendMessageContext& context) {
    std::vector<std::string> tableNames;
    if (context._chatType == "excel") {
        tableNames = getExcelWorksheetTables(context._sessionId, context._fileId);
    } else if (context._chatType == "database") {
        tableNames = getDatabaseTables(context._tableNames.empty() ? "" : context._tableNames[0]);
    } else {
        WRN("Unknown chat type: {}", context._chatType);
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::AI_PARAM_INVALID);
    }
    return tableNames;
}

// 连接 Id：excel → 默认连接（跨服务契约，见 dbConnMgr.h 的 EXCEL_DEFAULT_CONN_ID）；
// database → 请求参数携带
std::string AIMessageHandler::getDatabaseConnectId(const SendMessageContext& context) {
    if (context._chatType == "excel") {
        return "excel_default";
    } else if (context._chatType == "database") {
        return context._dbConnectId;
    }
    WRN("Unknown chat type: {}", context._chatType);
    throw chat2Data::Chat2DataException(chat2Data::ErrorCode::AI_PARAM_INVALID);
}

// Excel 场景：经文件子服务取该文件对应的 worksheet 数据库表名列表
std::vector<std::string> AIMessageHandler::getExcelWorksheetTables(const std::string& sessionId,
                                                                   const std::string& fileId) {
    std::vector<std::string> result;
    // 1. 取文件子服务信道
    auto channel = _svcChannels->getNode(FLAGS_file_service);
    if (!channel) {
        ERR("Failed to get FileService channel");
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::AI_SEND_MESSAGE_FAILED);
    }
    // 2. 构建 RPC 请求
    chat2Data::fileService::GetWorksheetDBTablesRequest request;
    request.set_request_id(chat2Data::Utils::generateUuid());
    request.set_session_id(sessionId);
    request.set_file_id(fileId);
    // 3. 发起 RPC 调用
    chat2Data::fileService::FileService_Stub stub(channel.get());
    chat2Data::fileService::GetWorksheetDBTablesResponse response;
    brpc::Controller controller;
    stub.GetWorksheetDBTables(&controller, &request, &response, nullptr);
    // 4. 检测结果
    if (controller.Failed()) {
        ERR("GetWorksheetDBTables RPC failed: {}", controller.ErrorText());
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::AI_SEND_MESSAGE_FAILED);
    }
    if (response.error_code() != 0) {
        ERR("GetWorksheetDBTables failed: errorCode={}, errorMsg={}",
            response.error_code(), response.error_msg());
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::AI_SEND_MESSAGE_FAILED);
    }
    // 5. 解析表名列表
    for (const auto& tableName : response.result().worksheet_dbtables()) {
        result.push_back(tableName);
    }
    INF("GetExcelWorksheetTables success: fileId={}, tableCount={}", fileId, result.size());
    return result;
}

// 逐表获取：表结构（GetTableStruct）+ 采样数据（GetSampleData，5 条）
// 这两样是分析提示词"数据上下文"的核心——模型据此生成精准 SQL
std::vector<TableSchemaInfo> AIMessageHandler::getDataSourceInfo(
        const std::string& dbConnectId, const std::string& sessionId,
        const std::vector<std::string>& tableNames) {
    std::vector<TableSchemaInfo> result;
    auto channel = _svcChannels->getNode(FLAGS_db_service);
    if (!channel) {
        ERR("Failed to get DatabaseService channel");
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::AI_SEND_MESSAGE_FAILED);
    }
    chat2Data::DatabaseService::DatabaseService_Stub stub(channel.get());
    for (const auto& tableName : tableNames) {
        TableSchemaInfo info;
        info._tableName = tableName;
        // 1. 表结构（格式化字符串，17 章 getTableStruct 的产物）
        {
            chat2Data::DatabaseService::GetTableStructRequest request;
            request.set_request_id(chat2Data::Utils::generateUuid());
            request.set_session_id(sessionId);
            request.set_db_connect_id(dbConnectId);
            request.set_table_name(tableName);
            chat2Data::DatabaseService::GetTableStructResponse response;
            brpc::Controller controller;
            stub.GetTableStruct(&controller, &request, &response, nullptr);
            if (controller.Failed() || response.error_code() != 0) {
                ERR("GetTableStruct failed: table={}, errorMsg={}",
                    tableName, response.error_msg());
                throw chat2Data::Chat2DataException(chat2Data::ErrorCode::AI_SEND_MESSAGE_FAILED);
            }
            info._schema = response.table_struct();
        }
        // 2. 采样数据（5 条，制表符分隔）
        {
            chat2Data::DatabaseService::GetSampleDataRequest request;
            request.set_request_id(chat2Data::Utils::generateUuid());
            request.set_session_id(sessionId);
            request.set_db_connect_id(dbConnectId);
            request.set_table_name(tableName);
            request.set_limit(SAMPLE_DATA_LIMIT);
            chat2Data::DatabaseService::GetSampleDataResponse response;
            brpc::Controller controller;
            stub.GetSampleData(&controller, &request, &response, nullptr);
            if (controller.Failed() || response.error_code() != 0) {
                ERR("GetSampleData failed: table={}, errorMsg={}",
                    tableName, response.error_msg());
                throw chat2Data::Chat2DataException(chat2Data::ErrorCode::AI_SEND_MESSAGE_FAILED);
            }
            info._sampleData = response.sample_data();
        }
        result.push_back(info);
    }
    return result;
}

// ---------- 提示词构建 ----------

// 构建分析提示词
std::string AIMessageHandler::buildAnalysisPrompt(const std::string& userInput,
                                                  const std::string& dbType,
                                                  const std::vector<TableSchemaInfo>& tables) {
    PromptTemplate prompt(ANALYSIS_PROMPT);
    // 多表时拼接到同一占位符
    std::string tableSchema, tableName, dataExample;
    for (const auto& table : tables) {
        tableSchema += table._schema + "\n\n";
        tableName += table._tableName + "\n\n";
        dataExample += table._sampleData + "\n\n";
    }
    prompt.setPlaceholder("DataBase", dbType);
    prompt.setPlaceholder("table_schema", tableSchema);
    prompt.setPlaceholder("table_name", tableName);
    prompt.setPlaceholder("data_example", dataExample);
    prompt.setPlaceholder("user_input", userInput);
    // AI9 修复：课件从不替换 {display_type}（模型会看到字面量占位符）——
    // 补全九种图表类型清单，与总结提示词 chartType 的可选值对齐
    prompt.setPlaceholder("display_type",
        "Table, BarChart, ColumnChart, LineChart, AreaChart, "
        "PieChart, DonutChart, ScatterChart, NumberDisplay");
    return prompt.build();
}

// 构建总结提示词（SQL 执行结果转 JSON 后注入）
std::string AIMessageHandler::buildSummaryPrompt(const std::string& userInput,
                                                 const std::string& sqlResult) {
    PromptTemplate prompt(SUMMARY_PROMPT);
    prompt.setPlaceholder("user_input", userInput);
    prompt.setPlaceholder("result_json", sqlResult);
    return prompt.build();
}

// ---------- 会话维护 ----------

// 获取会话标题：从第一条 assistant 消息的 <TITLE_START> 提取；
// 提取不到则用首条消息前 20 字
std::string AIMessageHandler::extractTitleFromFirstAssistantMessage(const std::string& chatSessionId) {
    // ★ AI1 修复：课件直接 _chatSdk->getSession(id)->_messages 解引用——
    //   getSession 可能返回 nullptr（会话不存在）→ 段错误
    auto session = _chatSdk->getSession(chatSessionId);
    if (!session) {
        WRN("Session not found in ChatSDK: chatSessionId={}", chatSessionId);
        return "";
    }
    // 遍历历史消息，找第一条 assistant 消息
    for (const auto& msg : session->_messages) {
        if (msg._role == "assistant") {
            std::string title = extractTagContent(msg._content, "<TITLE_START>", "<TITLE_END>");
            if (!title.empty()) {
                INF("Extract title from first assistant message: chatSessionId={}, title={}",
                    chatSessionId, title);
                return title;
            }
            // 无标题标签（如 plain 场景）：取首条消息前 20 字
            const std::string& firstContent = session->_messages[0]._content;
            size_t len = std::min<size_t>(20, firstContent.size());
            // UTF-8 边界修正（AI1 附加修复）：若切断点落在多字节字符的连续字节上，
            // 向前回退到字符起始——否则会产生"悬空首字节"（如 0xE5），
            // 写入 MySQL utf8mb4 列时报 1366 Incorrect string value
            while (len > 0 && len < firstContent.size() &&
                   (static_cast<unsigned char>(firstContent[len]) & 0xC0) == 0x80) {
                --len;
            }
            return firstContent.substr(0, len);
        }
    }
    WRN("No title found in first assistant message, chatSessionId={}", chatSessionId);
    return "";
}

// 更新会话：活跃时间 + 消息数(+2) + 标题（仅当尚无标题）
void AIMessageHandler::updateSessionActivity(const std::string& chatSessionId,
                                             const std::string& title) {
    auto sessionInfoOpt = _chatSessionMgr->getChatSessionBySessionId(chatSessionId);
    if (!sessionInfoOpt.has_value()) {
        ERR("Session not found, chatSessionId={}", chatSessionId);
        return;
    }
    ChatSessionInfo sessionInfo = sessionInfoOpt.value();
    // ★ AI2 修复：秒级时间戳（课件 steady_clock 纳秒 与 createTime 秒级混用 → 前端时间错乱）
    sessionInfo._updateTime = static_cast<int64_t>(time(nullptr));
    sessionInfo._messageCount += 2;          // 用户消息 + 模型回复
    if (!title.empty() && sessionInfo._title.empty()) {
        sessionInfo._title = title;
        INF("Update session title, chatSessionId={}, title={}", chatSessionId, title);
    }
    if (!_chatSessionMgr->saveChatSession(sessionInfo)) {
        ERR("Failed to save session, chatSessionId={}", chatSessionId);
        return;
    }
    INF("UpdateSessionActivity success: chatSessionId={}, messageCount={}",
        chatSessionId, sessionInfo._messageCount);
}

// ---------- chatDB 直改（AI3 修复） ----------

// 获取 chatDB.db 路径（AI5）：ChatSDK 的 SessionManager 默认 dbName="chatDB.db"
//（相对进程 CWD）→ 服务从 svc_aiService/build 启动时即 build/chatDB.db
std::string AIMessageHandler::getChatDBPath() {
    return (std::filesystem::current_path() / "chatDB.db").string();
}

// 打开 chatDB 的统一入口
// ★ AI3 修复：FULLMUTEX（内部串行化）+ busy_timeout 3 秒——
//   本处直改与 ChatSDK 的 DataManager 并发访问同一 sqlite 文件，
//   课件裸 sqlite3_open 遇并发直接 SQLITE_BUSY 失败
static sqlite3* openChatDB(const std::string& dbPath) {
    sqlite3* db = nullptr;
    int rc = sqlite3_open_v2(dbPath.c_str(), &db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (rc != SQLITE_OK) {
        ERR("Failed to open chatDB: {}", db ? sqlite3_errmsg(db) : "unknown error");
        if (db) {
            sqlite3_close(db);
        }
        return nullptr;
    }
    sqlite3_busy_timeout(db, 3000);   // 锁冲突时最多等 3 秒而非立即失败
    return db;
}

// 更新最近一条 assistant 消息为最终 JSON（历史消息渲染图表用）
void AIMessageHandler::updateLastAssistantMessage(const std::string& chatSessionId,
                                                  const std::string& finalJson) {
    std::string dbPath = getChatDBPath();
    if (dbPath.empty()) {
        ERR("Failed to get database path");
        return;
    }
    sqlite3* db = openChatDB(dbPath);
    if (!db) {
        return;
    }
    // 子查询定位该会话 timestamp 最大的 assistant 消息
    const char* updateSQL =
        "UPDATE messages SET content = ? WHERE message_id = ("
        "  SELECT message_id FROM messages"
        "  WHERE session_id = ? AND role = 'assistant'"
        "  ORDER BY timestamp DESC LIMIT 1)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, updateSQL, -1, &stmt, nullptr) != SQLITE_OK) {
        ERR("Failed to prepare update: {}", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }
    sqlite3_bind_text(stmt, 1, finalJson.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, chatSessionId.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        ERR("Failed to execute update: {}", sqlite3_errmsg(db));
    } else {
        INF("Update last assistant message success, chatSessionId={}", chatSessionId);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

// 删除指定会话的最后两条消息（邮件决策提示词与回复不入历史）
void AIMessageHandler::removeLastTwoMessages(const std::string& chatSessionId) {
    std::string dbPath = getChatDBPath();
    if (dbPath.empty()) {
        ERR("Failed to get database path");
        return;
    }
    sqlite3* db = openChatDB(dbPath);
    if (!db) {
        return;
    }
    const char* deleteSQL =
        "DELETE FROM messages WHERE session_id = ? AND rowid IN ("
        "  SELECT rowid FROM messages WHERE session_id = ?"
        "  ORDER BY timestamp DESC LIMIT 2)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, deleteSQL, -1, &stmt, nullptr) != SQLITE_OK) {
        ERR("Failed to prepare delete: {}", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }
    sqlite3_bind_text(stmt, 1, chatSessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, chatSessionId.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        ERR("Failed to execute delete: {}", sqlite3_errmsg(db));
    } else {
        INF("Remove last two messages success, chatSessionId={}", chatSessionId);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

// ---------- 普通消息 ----------

// 普通消息：直发模型 + 会话维护（标题/活跃时间/消息数）
void AIMessageHandler::sendPlainMessage(const SendMessageContext& context,
                                        SendMessageCallback writeChunk) {
    try {
        // 发送消息给模型（流式推给前端）
        std::string response = sendMessageToModel(context._chatSessionId, context._message, writeChunk);
        // 更新会话活跃时间和消息数，并更新会话标题
        std::string title = extractTitleFromFirstAssistantMessage(context._chatSessionId);
        updateSessionActivity(context._chatSessionId, title);
        // ★ AI11 修复：课件 plain 路径缺 SSE 结束标记（只有 excel/database 路径在
        //   主流程第 15 步发 done）→ 前端 EventSource 依赖 data: [DONE] 判定流结束，补发
        pushMessage("", true, writeChunk);
    } catch (const std::exception& e) {
        ERR("sendPlainMessage failed: {}", e.what());
        std::string errorMsg = "error: " + std::string(e.what());
        pushMessage(errorMsg, true, writeChunk);
    }
}

// ---------- 发送消息主流程（17 步） ----------

// 发送消息主流程：plain 直发 / excel·database 走四阶段（分析→SQL→执行→总结）
void AIMessageHandler::sendMessage(const SendMessageContext& context,
                                   SendMessageCallback writeChunk) {
    try {
        // 1. 普通聊天场景：直接发给模型，不走后续流程
        if (context._chatType == "plain") {
            INF("==========普通聊天场景===========");
            sendPlainMessage(context, writeChunk);
            return;
        }
        // 2. 获取表名列表（excel→文件服务；database→请求参数拆分）
        INF("==========获取数据库表列表===========");
        std::vector<std::string> tableNames = getDatabaseTables(context);
        if (tableNames.empty()) {
            WRN("获取数据库表失败，没有发现数据库表");
            throw chat2Data::Chat2DataException(chat2Data::ErrorCode::AI_SEND_MESSAGE_FAILED);
        }
        INF("获取数据库表成功，总共涉及到{}个表", tableNames.size());
        // 3. 数据库类型字符串（进分析提示词的 {DataBase}）
        std::string dbTypeStr = getDatabaseTypeString(context._dbType);
        INF("数据库类型：{}", dbTypeStr);
        // 4. 表结构信息（连接 Id + 逐表结构/采样）
        INF("==========获取数据源信息===========");
        std::string dbConnectId = getDatabaseConnectId(context);
        std::vector<TableSchemaInfo> tables =
            getDataSourceInfo(dbConnectId, context._sessionId, tableNames);
        INF("获取数据源信息成功，共获取到{}个表", tables.size());
        // 5. 构建分析提示词
        INF("==========构建分析提示词===========");
        std::string analysisPrompt = buildAnalysisPrompt(context._message, dbTypeStr, tables);
        INF("分析提示词：{}", analysisPrompt);
        // 6. 发送分析消息给模型（流式）
        INF("==========发送分析阶段消息===========");
        std::string analysisResponse =
            sendMessageToModel(context._chatSessionId, analysisPrompt, writeChunk);
        INF("==========分析阶段消息处理完成===========");
        // 7. 邮件请求检测（ToolCalling：<EMAIL_START>sendEmail<EMAIL_END>）
        if (isEmailRequest(analysisResponse)) {
            INF("==========模型分析结果为发送邮件请求===========");
            // 删除最后两条消息（决策性提示词与回复不入历史，避免污染会话）
            removeLastTwoMessages(context._chatSessionId);
            sendEmail(context, writeChunk);
            return;
        }
        // 8. 提取 SQL
        INF("==========从模型回复中提取SQL语句===========");
        std::string sql = extractSql(analysisResponse);
        if (sql.empty()) {
            WRN("SQL语句为空");
            throw chat2Data::Chat2DataException(chat2Data::ErrorCode::AI_SEND_MESSAGE_FAILED);
        }
        INF("提取到的SQL语句：{}", sql);
        // 9. 规范化 + 三关校验（不过则抛 AI_SEND_MESSAGE_FAILED）
        INF("==========规范化SQL语句===========");
        sql = normalizeSql(sql);
        INF("规范化后的SQL语句：{}", sql);
        // 10. 执行（修改类自动进临时表沙箱；失败以 error 字段进总结提示词）
        INF("==========开始执行SQL语句===========");
        SqlExecuteResult sqlResult = executeSql(context._sessionId, dbConnectId, sql);
        INF("==========执行SQL语句完成===========");
        // 11. 构建总结提示词（SQL 结果转 JSON 注入）
        INF("==========构建总结提示词===========");
        std::string summaryPrompt = buildSummaryPrompt(context._message, buildSqlResultJson(sqlResult));
        INF("总结提示词：{}", summaryPrompt);
        // 12. 发送总结消息给模型（流式）
        INF("==========发送总结阶段消息===========");
        std::string summaryResponse = sendMessageToModel(context._chatSessionId, summaryPrompt, writeChunk);
        INF("==========总结阶段消息处理完成===========");
        // 13. 提取总结内容与图表类型
        INF("==========构建最终响应===========");
        std::string summaryContent = getSummary(summaryResponse);
        if (summaryContent.empty()) {
            summaryContent = summaryResponse;
        }
        std::string displayType = getDisplayType(summaryResponse);
        // 14. 构建最终响应（保留 taskStatus/keyFindings；data 注入 SQL 结果）
        INF("总结内容：{}", summaryContent);
        INF("图表类型：{}", displayType);
        std::string finalResponse = buildFinalResponse(summaryResponse, displayType, sqlResult);
        INF("最终响应：{}", finalResponse);
        // 15. 发送最终响应（<CHART_DATA> 供前端识别图表数据）+ 结束标记
        INF("==========发送最终响应===========");
        pushMessage("<CHART_DATA>" + finalResponse + "</CHART_DATA>", false, writeChunk);
        pushMessage("", true, writeChunk);
        INF("==========发送最终响应完成===========");
        // 16. 更新会话活跃时间/消息数/标题
        std::string title = extractTitleFromFirstAssistantMessage(context._chatSessionId);
        updateSessionActivity(context._chatSessionId, title);
        // 17. 更新 chatDB 最近一条 assistant 消息为最终 JSON（历史消息渲染图表用）
        updateLastAssistantMessage(context._chatSessionId, finalResponse);
    } catch (const chat2Data::Chat2DataException& e) {
        ERR("Chat2DataException in sendMessage: code={}, msg={}",
            static_cast<int>(e.getErrorCode()), e.what());
        std::string errorMsg = "error: " + std::string(e.what());
        pushMessage(errorMsg, true, writeChunk);
    } catch (const std::exception& e) {
        ERR("std::exception in sendMessage: msg={}", e.what());
        std::string errorMsg = "error: " + std::string(e.what());
        pushMessage(errorMsg, true, writeChunk);
    }
}

// ---------- 邮件线（ToolCalling 的工具实现） ----------

// 检测模型回复是否为发送邮件请求：<EMAIL_START>sendEmail<EMAIL_END>
bool AIMessageHandler::isEmailRequest(const std::string& response) {
    const std::string emailStartTag = "<EMAIL_START>";
    size_t start = response.find(emailStartTag);
    if (start == std::string::npos) {
        return false;
    }
    size_t end = response.find("<EMAIL_END>", start + emailStartTag.size());
    if (end == std::string::npos) {
        return false;
    }
    return start < end;
}

// 发送邮件完整流程（8 步）
void AIMessageHandler::sendEmail(const SendMessageContext& context,
                                 SendMessageCallback writeChunk) {
    try {
        // 1. 获取用户邮箱（经用户子服务）
        INF("开始获取用户邮箱");
        std::string userEmail = getUserEmail(context);
        INF("用户邮箱获取成功: {}", userEmail);
        // 2. 获取最近两条 assistant 消息（总结消息 + 分析消息）
        INF("开始获取最近两条assistant消息");
        std::string summaryMessage;
        std::string analysisMessage;
        getRecentAssistantMessages(context._chatSessionId, summaryMessage, analysisMessage);
        // 3. 构建邮件参数 Json（question/analysis/summary）
        INF("开始构建生成邮件内容提示词");
        Json::Value emailParamJson = buildEmailParamsJson(analysisMessage, summaryMessage);
        // 4. 构建邮件生成提示词
        std::string emailPrompt = buildEmailPrompt(emailParamJson);
        INF("邮件内容提示词构建成功: {}", emailPrompt);
        // 5. 发送提示词给模型，生成邮件内容
        //    注意：邮件内容不推前端（writeChunk 传 nullptr）、不进消息列表（见步骤 6）
        INF("开始发送生成邮件内容提示词给模型");
        std::string emailResponse = sendMessageToModel(context._chatSessionId, emailPrompt, nullptr);
        INF("生成邮件内容模型回复: {}", emailResponse);
        // 6. 邮件生成的提示词与回复不入消息列表 → 删除最后两条
        //    （加上主流程步骤 7 已删的 2 条，共 4 条决策性消息不入历史）
        if (!emailResponse.empty()) {
            removeLastTwoMessages(context._chatSessionId);
        }
        // 7. 提取邮件主题和内容
        INF("开始从模型回复中提取邮件主题和内容");
        std::string subject("邮件主题");
        std::string content("邮件内容");
        buildEmailContent(emailResponse, subject, content);
        INF("邮件主题: {}, 邮件内容长度: {}", subject, content.size());
        // 8. 经通知子服务发送邮件
        INF("开始发送邮件...");
        sendEmail(userEmail, subject, content);
        INF("邮件发送成功");
        // 9. 给前端发送成功消息（<EMAIL_START> 协议标记，前端据此渲染提示条）
        pushMessage("<EMAIL_START>邮件发送成功，请注意查收<EMAIL_END>", false, writeChunk);
        pushMessage("", true, writeChunk);
        INF("发送邮件成功");
    } catch (const chat2Data::Chat2DataException& e) {
        ERR("发送邮件失败: code={}, msg={}", static_cast<int>(e.getErrorCode()), e.what());
        pushMessage("<EMAIL_START>邮件发送失败：" + std::string(e.what()) + "<EMAIL_END>",
                    true, writeChunk);
    } catch (const std::exception& e) {
        ERR("发送邮件失败: {}", e.what());
        pushMessage("<EMAIL_START>邮件发送失败：" + std::string(e.what()) + "<EMAIL_END>",
                    true, writeChunk);
    }
}

// 获取用户邮箱（经用户子服务 GetUserInfo）
std::string AIMessageHandler::getUserEmail(const SendMessageContext& context) {
    auto userChannel = _svcChannels->getNode(FLAGS_user_service);
    if (!userChannel) {
        ERR("Failed to get UserService channel");
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::AI_SEND_MESSAGE_FAILED);
    }
    chat2Data::userService::GetUserInfoRequest userRequest;
    userRequest.set_request_id(chat2Data::Utils::generateUuid());
    userRequest.set_session_id(context._sessionId);
    userRequest.set_user_id(context._userId);
    chat2Data::userService::UserService_Stub userStub(userChannel.get());
    chat2Data::userService::GetUserInfoResponse userResponse;
    brpc::Controller userController;
    userStub.GetUserInfo(&userController, &userRequest, &userResponse, nullptr);
    if (userController.Failed() || userResponse.error_code() != 0) {
        ERR("GetUserInfo failed: rpc={}, errorCode={}",
            userController.Failed() ? userController.ErrorText() : "ok",
            userResponse.error_code());
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::AI_SEND_MESSAGE_FAILED);
    }
    std::string userEmail = userResponse.result().user_info().email();
    if (userEmail.empty()) {
        ERR("User email is empty, userId={}", context._userId);
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::AI_SEND_MESSAGE_FAILED);
    }
    return userEmail;
}

// 获取最近两条 assistant 消息（倒序遍历：第 1 条=总结，第 2 条=分析）
void AIMessageHandler::getRecentAssistantMessages(const std::string& chatSessionId,
                                                  std::string& summaryMessage,
                                                  std::string& analysisMessage) {
    auto session = _chatSdk->getSession(chatSessionId);
    if (!session) {
        ERR("Session not found, sessionId={}", chatSessionId);
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::AI_SEND_MESSAGE_FAILED);
    }
    int assistantCount = 0;
    // 课件笔误：std::vector<Message> 未加命名空间（aiService 域无 Message）→ 编译错误
    const auto& messages = session->_messages;
    for (auto it = messages.rbegin(); it != messages.rend() && assistantCount < 2; ++it) {
        if ((*it)._role == "assistant") {
            if (assistantCount == 0) {
                summaryMessage = (*it)._content;     // 最近一条 = 总结
            } else {
                analysisMessage = (*it)._content;    // 次新一条 = 分析
            }
            assistantCount++;
        }
    }
    if (analysisMessage.empty() || summaryMessage.empty()) {
        ERR("Failed to find assistant messages, assistantCount={}", assistantCount);
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::AI_SEND_MESSAGE_FAILED);
    }
}

// 构建邮件参数 Json：从分析消息提取标题+分析内容，从总结消息提取总结
Json::Value AIMessageHandler::buildEmailParamsJson(const std::string& analysisMessage,
                                                   const std::string& summaryMessage) {
    // 1. 分析消息里提取标题与分析内容（提取不到用全文兜底）
    std::string title = extractTagContent(analysisMessage, "<TITLE_START>", "<TITLE_END>");
    if (title.empty()) {
        title = "数据分析报告";
    }
    std::string analysisContent = extractTagContent(analysisMessage, "<ANALYSIS_START>", "<ANALYSIS_END>");
    if (analysisContent.empty()) {
        analysisContent = analysisMessage;
    }
    // 2. 总结消息是 JSON，提取 summary 字段
    std::string summaryContent = getSummary(summaryMessage);
    // 3. 组装 {question, analysis, summary}
    //    （fileurl 本项目暂无下载链接能力，缺省——提示词已说明该字段可能不存在）
    Json::Value emailParam;
    emailParam["question"] = title;
    emailParam["analysis"] = analysisContent;
    emailParam["summary"] = summaryContent;
    return emailParam;
}

// 构建邮件生成提示词
std::string AIMessageHandler::buildEmailPrompt(const Json::Value& emailParamJson) {
    // 1. 标题兜底
    std::string title("消息标题");
    if (emailParamJson.isMember("question")) {
        title = emailParamJson["question"].asString();
    }
    // 2. 序列化邮件参数
    auto emailParamStrOpt = biteutil::JSON::serialize(emailParamJson);
    if (!emailParamStrOpt.has_value()) {
        ERR("Failed to serialize email param json");
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::AI_SEND_MESSAGE_FAILED);
    }
    // 3. 填充 {email_param} 与 {user_input}
    PromptTemplate prompt(EMAIL_PROMPT);
    prompt.setPlaceholder("user_input", title);
    prompt.setPlaceholder("email_param", emailParamStrOpt.value());
    return prompt.build();
}

// 从模型回复（纯 JSON）中提取邮件主题和内容
void AIMessageHandler::buildEmailContent(const std::string& emailResponse,
                                         std::string& subject, std::string& content) {
    auto emailJsonOpt = biteutil::JSON::unserialize(emailResponse);
    if (!emailJsonOpt.has_value() || !emailJsonOpt.value().isObject()) {
        ERR("Failed to parse email response, emailResponse={}", emailResponse);
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::AI_SEND_MESSAGE_FAILED);
    }
    if (emailJsonOpt.value().isMember("subject")) {
        subject = emailJsonOpt.value()["subject"].asString();
    }
    if (emailJsonOpt.value().isMember("content")) {
        content = emailJsonOpt.value()["content"].asString();
    }
}

// 发送邮件（调通知子服务 SendEmail——13 章的邮件能力在此复用）
void AIMessageHandler::sendEmail(const std::string& email, const std::string& subject,
                                 const std::string& content) {
    auto notifyChannel = _svcChannels->getNode(FLAGS_notify_service);
    if (!notifyChannel) {
        ERR("Failed to get NotifyService channel");
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::AI_SEND_MESSAGE_FAILED);
    }
    chat2Data::notifyService::SendEmailRequest emailRequest;
    emailRequest.set_request_id(chat2Data::Utils::generateUuid());
    emailRequest.set_to_email(email);
    emailRequest.set_subject(subject);
    emailRequest.set_content(content);
    chat2Data::notifyService::NotifyService_Stub notifyStub(notifyChannel.get());
    chat2Data::notifyService::SendEmailResponse emailResponse2;
    brpc::Controller notifyController;
    notifyStub.SendEmail(&notifyController, &emailRequest, &emailResponse2, nullptr);
    if (notifyController.Failed() || emailResponse2.error_code() != 0) {
        ERR("SendEmail failed: rpc={}, errorCode={}, errorMsg={}",
            notifyController.Failed() ? notifyController.ErrorText() : "ok",
            emailResponse2.error_code(), emailResponse2.error_msg());
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::AI_SEND_MESSAGE_FAILED);
    }
}

} // namespace aiService


#include "../include/ai_chat_sdk/DataManager.h"
#include "../include/ai_chat_sdk/util/myLog.h"
#include <memory>
#include <mutex>
#include <vector>



namespace ai_chat_sdk {

DataManager::DataManager(const std::string& dbName) 
    : _dbName(dbName)
    , _db(nullptr)
{
    // 创建并打开数据库
    int rc = sqlite3_open(dbName.c_str(), &_db);
    if(rc != SQLITE_OK){
        ERR("打开数据库失败：{}", sqlite3_errmsg(_db));
    }
    INFO("打开数据库成功：{}", dbName);

    // 初始化数据库表 - 创建会话表和消息表
    if(!initDataBase()){
        sqlite3_close(_db);
        _db = nullptr;
        ERR("初始化数据库表失败");
    }
}

DataManager::~DataManager(){
    if(_db){
        sqlite3_close(_db);
    }
}

// 初始化数据库
bool DataManager::initDataBase(){
    // 创建会话表
    std::string createSessionTable = R"(
        CREATE TABLE IF NOT EXISTS sessions (
            session_id TEXT PRIMARY KEY,
            model_name TEXT NOT NULL,
            create_time INTEGER NOT NULL,
            update_time INTEGER NOT NULL
        );
    )";

    // 执行创建Sessions表的SQL语句
    if(!executeSQL(createSessionTable)){
        return false;
    }

    // 创建消息表
    std::string createMessageTable = R"(
        CREATE TABLE IF NOT EXISTS messages (
            message_id TEXT PRIMARY KEY,
            session_id TEXT NOT NULL,
            role TEXT NOT NULL,
            content TEXT NOT NULL,
            timestamp INTEGER NOT NULL,
            FOREIGN KEY (session_id) REFERENCES sessions(session_id) ON DELETE CASCADE
        );
    )";

    // 执行创建Messages表的SQL语句
    if(!executeSQL(createMessageTable)){
        return false;
    }

    return true;
}

bool DataManager::executeSQL(const std::string& sql){
    if(!_db){
        ERR("数据库未初始化");
        return false;
    }

    char* errMsg = nullptr;
    int rc = sqlite3_exec(_db, sql.c_str(), nullptr, nullptr, &errMsg);
    if(rc != SQLITE_OK){
        ERR("执行SQL语句失败: {}", errMsg);
        sqlite3_free(errMsg);
        return false;
    }

    return true;
}

// 插入会话
bool DataManager::insertSession(const Session& session){
    std::lock_guard<std::mutex> lock(_mutex);

    // 构建SQL语句
    std::string insertSQL = R"(
        INSERT INTO sessions (session_id, model_name, create_time, update_time)
        VALUES (?, ?, ?, ?);
    )";

    // 准备SQL语句
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(_db, insertSQL.c_str(), -1, &stmt, nullptr);
    if(rc != SQLITE_OK){
        ERR("insertSession - 准备语句失败：{}", sqlite3_errmsg(_db));
        return false;
    }

    // 绑定参数
    sqlite3_bind_text(stmt, 1, session._sessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, session._modelName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, static_cast<int64_t>(session._createdAt));
    sqlite3_bind_int64(stmt, 4, static_cast<int64_t>(session._updatedAt));

    // 执行SQL语句
    rc = sqlite3_step(stmt);
    if(rc != SQLITE_DONE){
        ERR("insertSession - 执行语句失败：{}", sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        return false;
    }
    // 释放语句
    sqlite3_finalize(stmt);
    INFO("insertSession - 插入会话成功：{}", session._sessionId);
    return true;
}

// 获取指定sessionId的会话信息
std::shared_ptr<Session> DataManager::getSession(const std::string& sessionId)const{

    std::lock_guard<std::mutex> lock(_mutex);

    // 构建SQL语句
    std::string selectSQL = R"(
        SELECT model_name, create_time, update_time FROM sessions WHERE session_id = ?;
    )";

    // 准备SQL语句
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(_db, selectSQL.c_str(), -1, &stmt, nullptr);
    if(rc != SQLITE_OK){
        ERR("getSession - 准备语句失败：{}", sqlite3_errmsg(_db));
        return nullptr;
    }

    // 绑定参数
    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);

    // 执行SQL语句
    rc = sqlite3_step(stmt);
    if(rc != SQLITE_DONE){
        ERR("getSession - 执行语句失败：{}", sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        return nullptr;
    }

    // 从结果集中提取数据
    std::string modelName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    int64_t createTime = sqlite3_column_int64(stmt, 1);
    int64_t updateTime = sqlite3_column_int64(stmt, 2);

    // 创建会话对象
    auto session = std::make_shared<Session>(modelName);
    session->_sessionId = sessionId;
    session->_createdAt = static_cast<std::time_t>(createTime);
    session->_updatedAt = static_cast<std::time_t>(updateTime);

    // 释放语句
    sqlite3_finalize(stmt);
    INFO("getSession - 获取会话成功：{}", sessionId);

    // 获取该会话的所有消息
    session->_messages = getSessionMessages(sessionId);

    return session;
}

// 更新指定会话的时间戳
bool DataManager::updateSessionTimestamp(const std::string& sessionId, std::time_t timestamp){
    std::lock_guard<std::mutex> lock(_mutex);

    // 构建SQL语句
    std::string updateSQL = R"(
        UPDATE sessions SET update_time = ? WHERE session_id = ?;
    )";

    // 准备SQL语句
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(_db, updateSQL.c_str(), -1, &stmt, nullptr);
    if(rc != SQLITE_OK){
        ERR("updateSessionTimestamp - 准备语句失败：{}", sqlite3_errmsg(_db));
        return false;
    }

    // 绑定参数
    sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(timestamp));
    sqlite3_bind_text(stmt, 2, sessionId.c_str(), -1, SQLITE_TRANSIENT);

    // 执行SQL语句
    rc = sqlite3_step(stmt);
    if(rc != SQLITE_DONE){
        ERR("updateSessionTimestamp - 执行语句失败：{}", sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        return false;
    }

    // 释放语句
    sqlite3_finalize(stmt);
    INFO("updateSessionTimestamp - 更新会话时间戳成功：{}", sessionId);
    return true;
}

// 删除指定会话
bool DataManager::deleteSession(const std::string& sessionId){
    std::lock_guard<std::mutex> lock(_mutex);

    // 构建SQL语句
    std::string deleteSQL = R"(
        DELETE FROM sessions WHERE session_id = ?;
    )";

    // 准备SQL语句
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(_db, deleteSQL.c_str(), -1, &stmt, nullptr);
    if(rc != SQLITE_OK){
        ERR("deleteSession - 准备语句失败：{}", sqlite3_errmsg(_db));
        return false;
    }

    // 绑定参数
    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);

    // 执行SQL语句
    rc = sqlite3_step(stmt);
    if(rc != SQLITE_DONE){
        ERR("deleteSession - 执行语句失败：{}", sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        return false;
    }

    // 释放语句
    sqlite3_finalize(stmt);
    INFO("deleteSession - 删除会话成功：{}", sessionId);
    return true;
}

// 获取所有会话id
std::vector<std::string> DataManager::getAllSessionIds()const{
    std::lock_guard<std::mutex> lock(_mutex);

    // 构建SQL语句
    std::string selectSQL = R"(
        SELECT session_id FROM sessions ORDER BY update_time DESC;
    )";

    // 准备SQL语句
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(_db, selectSQL.c_str(), -1, &stmt, nullptr);
    if(rc != SQLITE_OK){
        ERR("getAllSessionIds - 准备语句失败：{}", sqlite3_errmsg(_db));
        return {};
    }

    std::vector<std::string> sessionIds;
    while(sqlite3_step(stmt) == SQLITE_ROW){
        std::string sessionId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        sessionIds.push_back(sessionId);
    }

    // 释放语句
    sqlite3_finalize(stmt);
    INFO("getAllSessionIds - 获取所有会话id成功, 会话总数：{}", sessionIds.size());
    return sessionIds;
}

// 获取所有session信息，并按照更新时间降序排列
std::vector<std::shared_ptr<Session>> DataManager::getAllSessions()const{
    std::lock_guard<std::mutex> lock(_mutex);

    // 构建SQL语句
    std::string selectSQL = R"(
        SELECT session_id, model_name, create_time, update_time FROM sessions ORDER BY update_time DESC;
    )";

    // 准备SQL语句
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(_db, selectSQL.c_str(), -1, &stmt, nullptr);
    if(rc != SQLITE_OK){
        ERR("getAllSessionIds - 准备语句失败：{}", sqlite3_errmsg(_db));
        return {};
    }

    std::vector<std::shared_ptr<Session>> sessions;
    while(sqlite3_step(stmt) == SQLITE_ROW){
        std::string sessionId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        std::string modelName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        int64_t createTime = sqlite3_column_int64(stmt, 2);
        int64_t updateTime = sqlite3_column_int64(stmt, 3);

        auto session = std::make_shared<Session>(modelName);
        session->_sessionId = sessionId;
        session->_createdAt = static_cast<std::time_t>(createTime);
        session->_updatedAt = static_cast<std::time_t>(updateTime);
        sessions.push_back(session);

        // 历史消息暂时不获取，需要时再通过会话id来进行获取
    }

    // 释放语句
    sqlite3_finalize(stmt);
    INFO("getAllSessions - 获取所有会话信息成功, 会话总数：{}", sessions.size());
    return sessions;
}

// 获取会话总数
size_t DataManager::getSessionCount()const
{
    std::lock_guard<std::mutex> lock(_mutex);

    // 准备SQL语句
    std::string selectSQL = R"(
        SELECT COUNT(*) FROM sessions;
    )";

    // 准备SQL语句
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(_db, selectSQL.c_str(), -1, &stmt, nullptr);
    if(rc != SQLITE_OK){
        ERR("getSessionCount - 准备语句失败：{}", sqlite3_errmsg(_db));
        return 0;
    }

    // 执行SQL语句
    rc = sqlite3_step(stmt);
    if(rc != SQLITE_ROW){
        ERR("getSessionCount - 执行语句失败：{}", sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        return 0;
    }

    // 获取会话总数
    size_t count = sqlite3_column_int64(stmt, 0);

    // 释放语句
    sqlite3_finalize(stmt);
    INFO("getSessionCount - 获取会话总数成功：{}", count);
    return count;
}

// 删除所有会话
bool DataManager::clearAllSessions(){
    std::lock_guard<std::mutex> lock(_mutex);

    // 构建SQL语句
    std::string deleteSQL = R"(
        DELETE FROM sessions;
    )";

    // 准备SQL语句
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(_db, deleteSQL.c_str(), -1, &stmt, nullptr);
    if(rc != SQLITE_OK){
        ERR("clearAllSessions - 准备语句失败：{}", sqlite3_errmsg(_db));
        return false;
    }

    // 执行SQL语句
    rc = sqlite3_step(stmt);
    if(rc != SQLITE_DONE){
        ERR("clearAllSessions - 执行语句失败：{}", sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        return false;
    }

    // 释放语句
    sqlite3_finalize(stmt);
    INFO("clearAllSessions - 删除所有会话成功");
    return true;
}

/////////////////////////////////////////////////////////////Messages///////////////////////////////////////
// 插入新消息--注意：插入消息时，需要更新会话的时间戳
bool DataManager::insertMessage(const std::string& sessionId, const Message& message){
    std::lock_guard<std::mutex> lock(_mutex);

    // 构建SQL语句
    std::string insertSQL = R"(
        INSERT INTO messages (message_id, session_id, role, content, timestamp)
        VALUES (?, ?, ?, ?, ?);
    )";

    // 准备SQL语句
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(_db, insertSQL.c_str(), -1, &stmt, nullptr);
    if(rc != SQLITE_OK){
        ERR("insertMessage - 准备语句失败：{}", sqlite3_errmsg(_db));
        return false;
    }

    // 绑定参数
    sqlite3_bind_text(stmt, 1, message._messageId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, message._role.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, message._content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, static_cast<int64_t>(message._timestamp));

    // 执行SQL语句
    rc = sqlite3_step(stmt);
    if(rc != SQLITE_DONE){
        ERR("insertMessage - 执行语句失败：{}", sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        return false;
    }

    // 同时更新session的update_time
    std::string updateSQL = R"(
        UPDATE sessions SET update_time = ? WHERE session_id = ?;
    )";

    // 准备SQL语句
    sqlite3_stmt* updateStmt;
    rc = sqlite3_prepare_v2(_db, updateSQL.c_str(), -1, &updateStmt, nullptr);
    if(rc != SQLITE_OK){
        ERR("insertMessage - 准备语句失败：{}", sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        return false;
    }

    // 绑定参数
    sqlite3_bind_int64(updateStmt, 1, static_cast<int64_t>(message._timestamp));
    sqlite3_bind_text(updateStmt, 2, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    

    // 执行SQL语句
    rc = sqlite3_step(updateStmt);
    if(rc != SQLITE_DONE){
        ERR("insertMessage - 执行语句失败：{}", sqlite3_errmsg(_db));
        sqlite3_finalize(updateStmt);
        return false;
    }

    // 释放语句
    sqlite3_finalize(stmt);
    sqlite3_finalize(updateStmt);
    INFO("insertMessage - 插入消息成功：{}", message._messageId);
    return true;
}

// 获取会话中的所有消息
std::vector<Message> DataManager::getSessionMessages(const std::string& sessionId)const
{
    std::lock_guard<std::mutex> lock(_mutex);

    // 准备SQL语句
    std::string selectSQL = R"(
        SELECT message_id, role, content, timestamp FROM messages WHERE session_id = ?;
    )";
        
    // 准备SQL语句
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(_db, selectSQL.c_str(), -1, &stmt, nullptr);
    if(rc != SQLITE_OK){
        ERR("getSessionMessages - 准备语句失败：{}", sqlite3_errmsg(_db));
        return {};
    }

    // 绑定参数
    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);

    // 执行SQL语句
    std::vector<Message> messages;
    while((rc = sqlite3_step(stmt)) == SQLITE_ROW){
        Message message;
        message._messageId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        message._role = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        message._content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        message._timestamp = static_cast<std::time_t>(sqlite3_column_int64(stmt, 3));
        messages.push_back(message);
    }

    if(rc != SQLITE_DONE){
        ERR("getSessionMessages - 执行语句失败：{}", sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        return {};
    }

    // 释放语句
    sqlite3_finalize(stmt);
    return messages;
}

// 删除制定会话的历史消息
bool DataManager::deleteSessionMessages(const std::string& sessionId){
    std::lock_guard<std::mutex> lock(_mutex);

    // 构建SQL语句
    std::string deleteSQL = R"(
        DELETE FROM messages WHERE session_id = ?;
    )";

    // 准备SQL语句
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(_db, deleteSQL.c_str(), -1, &stmt, nullptr);
    if(rc != SQLITE_OK){
        ERR("deleteSessionMessages - 准备语句失败：{}", sqlite3_errmsg(_db));
        return false;
    }   

    // 绑定参数
    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);

    // 执行SQL语句
    rc = sqlite3_step(stmt);
    if(rc != SQLITE_DONE){
        ERR("deleteSessionMessages - 执行语句失败：{}", sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        return false;
    }

    // 释放语句
    sqlite3_finalize(stmt);
    INFO("deleteSessionMessages - 删除会话消息成功：{}", sessionId);
    return true;
}


} // end ai_chat_sdk

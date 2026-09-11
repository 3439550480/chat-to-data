#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include "../proto/protoCode/aiService.pb.h"

namespace aiService {

// Builder 入参（与驱动层 odb/redis 配置字段名不同，Server build 内转换）
struct MysqlConfig {
    std::string _host;                 // MySQL主机地址
    std::string _user;                 // MySQL用户名
    std::string _passwd;               // MySQL密码
    std::string _db;                   // MySQL数据库名
    unsigned int _port;                // MySQL端口号
    std::string _cset;                 // MySQL字符集
    unsigned int _connectionPoolSize;  // MySQL连接池大小
};
struct RedisConfig {
    std::string _host;                 // Redis主机地址
    int _port;                         // Redis端口号
    std::string _passwd;               // Redis密码
    int _db;                           // Redis数据库索引
    size_t _connectionPoolSize;        // Redis连接池大小
};
struct RegisterCenterConfig {
    std::string _etcdAddr;             // ETCD地址
    std::string _serviceName;          // 服务名称
    std::string _serviceAddr;          // 服务地址
};

// 会话元数据（业务层 ↔ 数据层载体）
struct ChatSessionInfo {
    std::string _chatSessionId;        // 聊天会话Id
    std::string _userId;               // 用户Id
    std::string _title;                // 会话标题
    int64_t _createTime;               // 会话创建时间
    int64_t _updateTime;               // 会话最近活跃时间
    int _messageCount;                 // 消息总数
    std::string _modelName;            // 模型名称
    std::string _fileId;               // 关联文件Id
    std::string _sessionType;          // 会话类型：excel/database
    std::string _dbConnectionInfo;     // 数据库连接信息JSON
};

// 历史消息（user = 用户消息，assistant = AI消息）
struct HistoryMessageInfo {
    std::string _messageId;            // 消息Id
    std::string _role;                 // 角色：user/assistant
    std::string _content;              // 消息内容
    int64_t _timestamp;                // 消息时间戳
};

// 模型列表项（GetModels 接口）
struct ModelInfo {
    std::string _name;                 // 模型名称
    std::string _desc;                 // 模型描述
};

// 新建会话结果
struct CreateSessionResult {
    std::string _chatSessionId;        // 聊天会话Id
    std::string _model;                // 模型名称
};

// 会话详情（GetSessions / GetSession 用）
struct SessionDetailInfo {
    std::string _id;                   // 会话Id
    std::string _model;                // 模型名称
    std::string _title;                // 会话标题
    int64_t _createdAt;                // 创建时间
    int64_t _updatedAt;                // 最近活跃时间
    int _messageCount;                 // 消息数量
    std::string _firstUserMessageContent;  // 首条消息内容(不超过20字)
    std::string _sessionType;          // 会话类型：plain/excel/database
    std::string _dbConnectionInfo;     // 数据库连接信息JSON
};

// 会话历史消息结果
struct SessionHistoryResult {
    std::string _fileId;               // 可选，仅在会话关联了文件时存在
    std::string _sessionType;          // 会话类型：plain/excel/database
    std::string _dbConnectionInfo;     // 数据库连接信息JSON
    std::vector<HistoryMessageInfo> _messages;  // 历史消息列表
};

// 发送消息上下文（H10 发送消息核心流程的全部输入）
struct SendMessageContext {
    std::string _requestId;            // 请求ID
    std::string _sessionId;            // 会话ID
    std::string _chatSessionId;        // AI聊天会话ID
    std::string _userId;               // 用户ID
    std::string _message;              // 用户消息
    std::string _chatType;             // 聊天类型：plain/excel/database
    std::string _fileId;               // 文件ID（Excel场景）
    chat2Data::AiService::DataType _dbType;  // 数据类型（数据库场景）
    std::string _dbConnectId;          // 数据库连接ID（数据库场景）
    std::vector<std::string> _tableNames;    // 表名列表（数据库场景）
};

// 表结构信息（喂给分析提示词：表结构 + 采样数据）
struct TableSchemaInfo {
    std::string _tableName;            // 表名
    std::string _schema;               // 表结构（列信息）
    std::string _sampleData;           // 采样数据（5条）
};

// SQL执行请求（预留结构，实际执行走 aiMessageHandler 的 SqlExecuteResult 路径）
struct SqlExecuteRequest {
    std::string _dbConnectId;          // 数据库连接ID
    chat2Data::AiService::DataType _dbType;  // 数据库类型
    std::string _sql;                  // SQL语句
};

// SQL执行结果（喂给总结提示词 + 组装最终响应的 data 字段）
struct SqlExecuteResult {
    bool _success;                     // 是否成功
    bool _isQuery;                     // 是否查询语句
    std::string _errorMsg;             // 错误信息
    std::vector<std::string> _columns;           // 列名列表
    std::vector<std::string> _columnTypes;       // 列类型列表
    std::vector<std::vector<std::string>> _rows; // 行数据
    int _affectedRows;                 // 影响的行数
};

} // namespace aiService

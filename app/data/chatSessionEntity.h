#pragma once

#include <string>
#include <cstddef>
#include <odb/core.hxx>
#include <odb/nullable.hxx>

// 数据库表名：tbl_chatSession（聊天会话元数据表）
// 职责：把 ChatSDK 内部"无用户概念"的聊天会话，按用户隔离并补充业务元数据
//      （标题/消息数/关联文件/场景类型/数据库连接信息）；
//      聊天消息本体仍由 ChatSDK 管理（chatDB.db 的 SQLite），本表只管"会话账本"
#pragma db object table("tbl_chatSession")
class ChatSessionEntity {
public:
    ChatSessionEntity() {}
    ChatSessionEntity(const std::string& chatSessionId,
                      const std::string& userId,
                      const std::string& title,
                      int64_t createTime,
                      int64_t updateTime,
                      int messageCount,
                      const std::string& modelName,
                      const std::string& fileId,
                      const std::string& sessionType,
                      const std::string& dbConnectionInfo)
        : _chatSessionId(chatSessionId), _userId(userId), _title(title),
          _createTime(createTime), _updateTime(updateTime), _messageCount(messageCount),
          _modelName(modelName), _fileId(fileId), _sessionType(sessionType),
          _dbConnectionInfo(dbConnectionInfo) {}

    unsigned long long id() const { return _id; }
    void setId(unsigned long long id) { _id = id; }

    // 与 ChatSDK 内部（chatDB.db 的 sessions 表）的会话 Id 保持一致；
    // 唯一索引：所有"取会话/删会话/缓存键"都走它，是最高频查询入口
    std::string chatSessionId() const { return _chatSessionId; }
    void setChatSessionId(const std::string& chatSessionId) { _chatSessionId = chatSessionId; }

    // 所属用户：会话按用户隔离的依据（ChatSDK 不区分用户，靠本表补上）
    std::string userId() const { return _userId; }
    void setUserId(const std::string& userId) { _userId = userId; }

    // 会话标题：创建时为空串；用户发首条消息后，由 AI 服务取模型输出里
    // <TITLE_START> 中的标题（或用户消息前 20 字）回填，此后不再变更
    std::string title() const { return _title; }
    void setTitle(const std::string& title) { _title = title; }

    // 时间戳统一用秒级（AI2 修复：课件 updateTime 误用 steady_clock 纳秒，
    // 与 createTime 的秒级混用会导致前端时间显示错乱）
    int64_t createTime() const { return _createTime; }
    void setCreateTime(int64_t createTime) { _createTime = createTime; }

    int64_t updateTime() const { return _updateTime; }
    void setUpdateTime(int64_t updateTime) { _updateTime = updateTime; }

    // 消息总数：每次新增消息（用户+助手共 2 条）后 +2，会话列表展示用
    int messageCount() const { return _messageCount; }
    void setMessageCount(int messageCount) { _messageCount = messageCount; }

    // 会话绑定的模型名（创建会话时选定，ChatSDK 按它路由 Provider）
    std::string modelName() const { return _modelName; }
    void setModelName(const std::string& modelName) { _modelName = modelName; }

    // AI4 决策：fileId 用 odb::nullable —— NULL = 未关联文件。
    // 为什么这里不能用 ㉟ 的"空串语义"：本列带外键（FK → tbl_fileInfo.fileId，
    // ON DELETE CASCADE：文件删除时本表对应会话行自动级联删除），空串会违反外键约束；
    // 只有 NULL 才能同时表达"无关联"且满足外键。无文件的普通对话场景插入 NULL。
    std::string fileId() const { return _fileId.null() ? "" : *_fileId; }
    void setFileId(const std::string& fileId) {
        if (fileId.empty()) {
            _fileId.reset();          // 空串输入 = 解除关联（写 NULL）
        } else {
            _fileId = fileId;
        }
    }

    std::string sessionType() const { return _sessionType; }
    void setSessionType(const std::string& sessionType) { _sessionType = sessionType; }

    // AI4 决策：按 ㉟ 惯例用 std::string 空串（空串 = 非 database 场景），
    // 删掉课件的 null 限定（ODB 拒绝 std::string 搭配 null —— 真编译错误）
    // 仅 database 场景有值：JSON 形式的数据库连接信息，打开历史会话时用于重建连接
    std::string dbConnectionInfo() const { return _dbConnectionInfo; }
    void setDbConnectionInfo(const std::string& dbConnectionInfo) {
        _dbConnectionInfo = dbConnectionInfo;
    }

private:
    friend class odb::access;

    // 主键：自增 id（业务查询一律走 chatSessionId 唯一索引，本主键仅作行标识）
    #pragma db id auto
    unsigned long long _id;

    #pragma db unique
    #pragma db column("chatSessionId") type("VARCHAR(32) CHARACTER SET utf8mb4")
    std::string _chatSessionId;

    #pragma db column("userId") type("VARCHAR(32) CHARACTER SET utf8mb4")
    std::string _userId;

    // AI4：title 用 std::string 空串语义（无 NULL 需求），删掉课件的 null 限定
    #pragma db column("title") type("TEXT CHARACTER SET utf8mb4")
    std::string _title;

    #pragma db column("createTime") type("BIGINT UNSIGNED")
    int64_t _createTime;

    #pragma db column("updateTime") type("BIGINT UNSIGNED")
    int64_t _updateTime;

    #pragma db column("messageCount") type("INT UNSIGNED")
    int _messageCount;

    #pragma db column("modelName") type("TEXT CHARACTER SET utf8mb4")
    std::string _modelName;

    // AI4 决策：fileId 用 odb::nullable —— NULL = 未关联文件；
    // 外键（建表后手动追加，见 data/sql/chatSessionEntity.sql 追加段）：
    //   fileId → tbl_fileInfo.fileId ON DELETE CASCADE
    //   文件删除时本表对应会话行自动级联删除（纯级联场景，FK 恰当；
    //   与 15 章方案 A 拒绝 FK 的"多行映射需显式管理"场景不同）
    #pragma db column("fileId") type("VARCHAR(32) CHARACTER SET utf8mb4") null
    odb::nullable<std::string> _fileId;

    // AI4：sessionType 必填（决定前端跳转），不用 nullable
    #pragma db column("sessionType") type("VARCHAR(20) CHARACTER SET utf8mb4")
    std::string _sessionType;

    // AI4：dbConnectionInfo 用空串语义（database 场景才有值），删 null 限定
    #pragma db column("dbConnectionInfo") type("TEXT CHARACTER SET utf8mb4")
    std::string _dbConnectionInfo;
};

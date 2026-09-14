#pragma once
#include <jsoncpp/json/value.h>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <gflags/gflags.h>
#include <ai_chat_sdk/ChatSDK.h>
#include <bite_scaffold/rpc.h>
#include "../proto/protoCode/dbService.pb.h"
#include "../proto/protoCode/fileService.pb.h"
#include "../proto/protoCode/notifyService.pb.h"
#include "../proto/protoCode/userService.pb.h"
#include "common.h"

// 声明 gflags 变量（main.cc 中 DEFINE；四条依赖线的调用 key）
DECLARE_string(file_service);
DECLARE_string(db_service);
DECLARE_string(user_service);
DECLARE_string(notify_service);

namespace aiService {

class ChatSessionMgr;

// AI 消息处理器：发送消息的核心业务逻辑（19 章）
// 三条对话线：plain（直发模型）/ excel（表名经文件服务）/ database（表名来自请求参数）
class AIMessageHandler {
public:
    // writeChunk: 推流回调（chunk, done）——AIBusiness 里负责 SSE 格式化
    using SendMessageCallback = std::function<void(const std::string&, bool)>;

    AIMessageHandler(std::shared_ptr<ai_chat_sdk::ChatSDK> chatSdk,
                     std::shared_ptr<ChatSessionMgr> chatSessionMgr,
                     std::shared_ptr<biterpc::SvcChannels> svcChannels);

    // 发送消息主流程（17 步：plain 直发 / excel·database 四阶段分析→SQL→总结）
    void sendMessage(const SendMessageContext& context, SendMessageCallback writeChunk);

private:
    // ---------- 场景数据获取 ----------
    // Excel 场景：经文件子服务取该文件对应的 worksheet 数据库表名列表
    std::vector<std::string> getExcelWorksheetTables(const std::string& sessionId,
                                                     const std::string& fileId);
    // 数据库场景：请求参数中的表名串（逗号分隔）拆分
    std::vector<std::string> getDatabaseTables(const std::string& tableNames);
    // 表名列表总入口（按 chatType 分派）
    std::vector<std::string> getDatabaseTables(const SendMessageContext& context);
    // 数据库连接 Id：excel → "excel_default"；database → 请求参数
    std::string getDatabaseConnectId(const SendMessageContext& context);
    // 数据库类型枚举 → 提示词用字符串
    std::string getDatabaseTypeString(chat2Data::AiService::DataType dbType);
    // 表结构 + 采样数据（经数据库子服务，逐表两次 RPC）
    std::vector<TableSchemaInfo> getDataSourceInfo(const std::string& dbConnectId,
                                                   const std::string& sessionId,
                                                   const std::vector<std::string>& tableNames);

    // ---------- 提示词构建 ----------
    std::string buildAnalysisPrompt(const std::string& userInput, const std::string& dbType,
                                    const std::vector<TableSchemaInfo>& tables);
    std::string buildSummaryPrompt(const std::string& userInput, const std::string& sqlResult);
    std::string buildEmailPrompt(const Json::Value& emailParamJson);

    // ---------- 模型交互与解析 ----------
    // 发送消息给模型（流式），累积完整响应并实时推流
    std::string sendMessageToModel(const std::string& chatSessionId, const std::string& message,
                                   SendMessageCallback writeChunk);
    // 提取 <tagStart>...<tagEnd> 之间的内容
    std::string extractTagContent(const std::string& response, const std::string& tagStart,
                                  const std::string& tagEnd);
    // 从模型响应中提取 SQL 语句
    std::string extractSql(const std::string& response);
    // SQL 规范化（去注释/trim）+ 三关校验，不过则抛 AI_SEND_MESSAGE_FAILED
    std::string normalizeSql(const std::string& sql);
    // 经数据库子服务执行 SQL（修改类会走临时表沙箱——17 章能力复用）
    SqlExecuteResult executeSql(const std::string& sessionId, const std::string& dbConnectId,
                                const std::string& sql);

    // ---------- 结果组装 ----------
    // 构建SQL执行结果Json（喂给总结提示词）
    std::string buildSqlResultJson(const SqlExecuteResult& sqlResult);
    // 获取总结内容
    std::string getSummary(const std::string& summaryMessage);
    // 获取图表类型
    std::string getDisplayType(const std::string& summaryMessage);
    // 最终响应 = 模型总结 JSON + data(SQL 结果)；单行序列化（防 SSE 断行）
    std::string buildFinalResponse(const std::string& summaryResponseJson,
                                   const std::string& displayType,
                                   const SqlExecuteResult& sqlResult);
    // 推送消息给前端
    void pushMessage(const std::string& content, bool done, SendMessageCallback writeChunk);

    // ---------- 普通消息（H11） ----------
    void sendPlainMessage(const SendMessageContext& context, SendMessageCallback writeChunk);

    // ---------- 邮件线（H12，ToolCalling） ----------
    // 检测模型回复是否为发送邮件请求 <EMAIL_START>sendEmail<EMAIL_END>
    bool isEmailRequest(const std::string& response);
    // 发送邮件（完整流程：邮箱→最近两条→参数→提示词→模型→发信）
    void sendEmail(const SendMessageContext& context, SendMessageCallback writeChunk);
    // 发送邮件（调邮件通知子服务）
    void sendEmail(const std::string& email, const std::string& subject, const std::string& content);
    // 获取用户邮箱（经用户子服务）
    std::string getUserEmail(const SendMessageContext& context);
    // 获取最近两条 assistant 消息（分析消息 + 总结消息）
    void getRecentAssistantMessages(const std::string& chatSessionId,
                                    std::string& summaryMessage, std::string& analysisMessage);
    // 构建邮件参数 Json
    Json::Value buildEmailParamsJson(const std::string& analysisMessage,
                                     const std::string& summaryMessage);
    // 从模型回复中提取邮件主题和内容
    void buildEmailContent(const std::string& emailResponse, std::string& subject,
                           std::string& content);

    // ---------- 会话维护与 chatDB 直改（H11） ----------
    // 更新会话活跃时间和消息数，并更新会话标题
    void updateSessionActivity(const std::string& chatSessionId, const std::string& title = "");
    // 从聊天会话的第一条助手消息中提取标题
    std::string extractTitleFromFirstAssistantMessage(const std::string& chatSessionId);
    // 更新 ChatSDK 底层 sqlite 中最近一条 assistant 消息为最终 json
    void updateLastAssistantMessage(const std::string& chatSessionId, const std::string& finalJson);
    // 删除 ChatSDK 底层 sqlite 中指定会话的最后两条消息
    void removeLastTwoMessages(const std::string& chatSessionId);
    // 获取 chatDB.db 文件路径
    std::string getChatDBPath();

private:
    std::shared_ptr<ai_chat_sdk::ChatSDK> _chatSdk;           // ChatSDK 实例
    std::shared_ptr<ChatSessionMgr> _chatSessionMgr;          // 会话管理器
    std::shared_ptr<biterpc::SvcChannels> _svcChannels;       // RPC 通信
    const static int SAMPLE_DATA_LIMIT = 5;                   // 采样数据条数
};

} // namespace aiService

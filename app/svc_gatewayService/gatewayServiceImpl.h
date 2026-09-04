#pragma once

#include <string>
#include <memory>
#include <httplib.h>
#include <gflags/gflags.h>
#include <bite_scaffold/rpc.h>
#include <bite_scaffold/etcd.h>

// 声明gflags变量（在main.cc中定义）
DECLARE_string(user_service);
DECLARE_string(file_service);
DECLARE_string(db_service);
DECLARE_string(ai_service);

namespace GatewayService {

// HTTP服务实现类
// 负责：HTTP路由处理、权限校验、RPC调用、请求转发
class GatewayServiceImpl {
public:
    //核心作用是禁止编译器执行隐式类型转换，强制要求程序员必须显式地写出转换意图。
    explicit GatewayServiceImpl(std::shared_ptr<biterpc::SvcChannels> svcChannels,
                                std::shared_ptr<bitesvc::SvcWatcher> serviceWatcher);
    ~GatewayServiceImpl();

    // 配置HTTP路由
    // 配置路由就是告诉服务器："当收到某个特定请求时，应该执行哪段代码来响应"。
    void bindRoutes(httplib::Server& server);

private:
    // ==================== 响应工具函数 ====================
    // 课件原版未在头文件中声明这两个函数，导致 .cc 中调用时报 "was not declared in this scope"，此处补充
    // 发送JSON响应
    void sendJsonResponse(httplib::Response& res, const std::string& jsonData, int statusCode);
    // 发送错误响应
    void sendErrorResponse(httplib::Response& res,
                           const std::string& requestId,
                           int errorCode,
                           const std::string& errorMsg,
                           int statusCode);

    // ==================== 健康检测 ====================
    void handleHealthCheck(const httplib::Request& req, httplib::Response& res);

    // ==================== 用户服务9个 ====================
    // 检测用户昵称是否唯一
    void handleValidNickname(const httplib::Request& req, httplib::Response& res);
    // 检测用户邮箱是否唯一
    void handleValidEmail(const httplib::Request& req, httplib::Response& res);
    // 用户注册
    void handleUserRegister(const httplib::Request& req, httplib::Response& res);
    // 会话登录
    void handleSessionLogin(const httplib::Request& req, httplib::Response& res);
    // 密码登录(用户昵称或邮箱)
    void handlePasswordLogin(const httplib::Request& req, httplib::Response& res);
    // 获取验证码
    void handleGetVerifyCode(const httplib::Request& req, httplib::Response& res);
    // 验证码登录
    void handleVerifyCodeLogin(const httplib::Request& req, httplib::Response& res);
    // 退出登录
    void handleLogout(const httplib::Request& req, httplib::Response& res);
    // 获取用户信息
    void handleGetUserInfo(const httplib::Request& req, httplib::Response& res);

    // ==================== 文件服务9个 ====================
    // 上传文件信息
    void handleFileUploadInfo(const httplib::Request& req, httplib::Response& res);
    // 获取文件信息
    void handleGetFileInfo(const httplib::Request& req, httplib::Response& res);
    // 上传文件数据
    void handleFileUpload(const httplib::Request& req, httplib::Response& res);
    // 下载文件
    void handleFileDownload(const httplib::Request& req, httplib::Response& res);
    // 删除文件
    void handleFileDelete(const httplib::Request& req, httplib::Response& res);
    // 预览Excel文件
    void handleFilePreview(const httplib::Request& req, httplib::Response& res);
    // 获取文件列表
    void handleGetFileList(const httplib::Request& req, httplib::Response& res);
    // 文件-会话映射
    void handleFileChatMap(const httplib::Request& req, httplib::Response& res);
    // 上传SQLite文件
    void handleSqliteUpload(const httplib::Request& req, httplib::Response& res);

    // ==================== 数据库服务5个 ====================
    // 连接数据库
    void handleDbConnect(const httplib::Request& req, httplib::Response& res);
    // 断开数据库连接
    void handleDbDisconnect(const httplib::Request& req, httplib::Response& res);
    // 获取数据库表列表
    void handleGetDbTables(const httplib::Request& req, httplib::Response& res);
    // 获取表数据
    void handleGetTableData(const httplib::Request& req, httplib::Response& res);
    // 获取数据库连接状态
    void handleGetConnectionStatus(const httplib::Request& req, httplib::Response& res);

    // ==================== AI服务6个 ====================
    // 获取模型列表
    void handleGetModels(const httplib::Request& req, httplib::Response& res);
    // 新建聊天会话
    void handleCreateChatSession(const httplib::Request& req, httplib::Response& res);
    // 获取聊天会话列表
    void handleGetChatSessionLists(const httplib::Request& req, httplib::Response& res);
    // 获取指定用户指定聊天会话历史消息
    void handleGetHistory(const httplib::Request& req, httplib::Response& res);
    // 删除指定用户指定聊天会话
    void handleDeleteChatSession(const httplib::Request& req, httplib::Response& res);
    // 发送消息(流式)
    void handleAiChat(const httplib::Request& req, httplib::Response& res);

private:
    std::shared_ptr<biterpc::SvcChannels> _svcChannels;    // RPC服务信道，用于获取后端子服务节点
    std::shared_ptr<bitesvc::SvcWatcher> _serviceWatcher;  // 服务发现监控器
    std::string _exePath;                                  // 静态资源路径(可执行文件所在目录/www)
};

} // end GatewayService

#include <ctime>
#include <chrono>
#include <sstream>
#include <unistd.h>
#include <limits.h>
#include <bite_scaffold/log.h>
#include <jsoncpp/json/writer.h>
#include <jsoncpp/json/reader.h>
#include <bite_scaffold/rpc.h>
#include <bite_scaffold/util.h>
#include "../proto/protoCode/userService.pb.h"
#include "../proto/protoCode/fileService.pb.h"
#include "../proto/protoCode/dbService.pb.h"
#include "../proto/protoCode/aiService.pb.h"
#include "gatewayServiceImpl.h"

namespace GatewayService {

//在服务启动阶段，自动计算出前端静态文件（HTML/CSS/JS等）所在的绝对路径，并将其保存到 _exePath 成员变量中。

GatewayServiceImpl::GatewayServiceImpl(std::shared_ptr<biterpc::SvcChannels> svcChannels,
                                       std::shared_ptr<bitesvc::SvcWatcher> serviceWatcher)
    : _svcChannels(svcChannels), _serviceWatcher(serviceWatcher) {
    //Step 1: 读取可执行文件绝对路径
        ///proc/self/exe 是 Linux 特有的符号链接，指向当前进程的可执行文件。
        // readlink 不会在结果末尾添加 \0，返回值 count 是实际字节数。这是后续所有安全处理的根源。
    char result[PATH_MAX];
    // readlink 是一个 POSIX 标准系统调用，专门用于读取符号链接（symlink）本身指向的目标路径。获取当前可执行程序的路径（readlink 不添加字符串结束符，因此后续必须用 count 构造 string）
    ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);

    //Step 2: 安全构造字符串并截取目录
        //count != -1 的判断 —— readlink 失败返回 -1，此时 result 内容未定义，绝不能拿来用，所以走 else 兜底到 /www。失败场景很罕见但必须兜住，否则静态资源路径会变成随机字节。
        //std::string(result, count)：这是此代码最关键的修复点。注释明确提到原版直接传 result（无 \0），fmt 库会当作 C 字符串解析导致越界读取/崩溃。改为 (ptr, len) 构造是正确做法。
        // find_last_of('/') + substr：从 /opt/app/bin/gateway → /opt/app/bin。
    if (count != -1) {
        _exePath = std::string(result, count);
        size_t lastSlash = _exePath.find_last_of('/');
        if (lastSlash != std::string::npos) {
            _exePath = _exePath.substr(0, lastSlash);
        }

    //Step 3: 拼接静态资源子目录
        INF("Executable path: {}", _exePath);
        _exePath += "/www";
        // 课件原版此处传 result（char[] 无 '\0' 结尾，fmt 按 C 字符串解析会越界读），改为传 _exePath
        INF("Static resources path: {}", _exePath);
    }
    //Step 4: 降级兜底
     else {
        _exePath = "/www";
        WRN("Failed to get executable path, using default static resources path: {}", _exePath);
    }
    INF("GatewayServiceImpl initialized");
}

GatewayServiceImpl::~GatewayServiceImpl() {
    INF("GatewayServiceImpl destroyed");
}

void GatewayServiceImpl::bindRoutes(httplib::Server& server) {
    INF("Binding HTTP routes...");
    // 设置静态资源的路径（基于可执行程序路径）
    INF("Setting static resources path to {}", _exePath);
    server.set_mount_point("/", _exePath);// 挂载 ./build/www，前端页面放这里才能被访问

    // 路由绑定流程说明：
    // 网关共实现30个HTTP接口，分为5大类：
    // 1. 健康检测接口（1个）：用于检查网关服务是否正常运行
    // 2. 用户子服务接口（9个）：处理用户注册、登录、信息查询等
    // 3. 文件子服务接口（9个）：处理文件上传、下载、预览等
    // 4. 存储子服务接口（5个）：处理数据库连接、数据查询等
    // 5. AI子服务接口（6个）：处理聊天会话、模型交互等
    //
    // 每个路由使用Lambda表达式捕获this指针，将请求转发到对应的处理函数
    // 处理函数内部会调用SvcChannels获取后端服务的Channel，发起RPC请求

    // ==================== 健康检测接口 ====================
    server.Get("/health", [this](const httplib::Request& req, httplib::Response& res) {
        handleHealthCheck(req, res);
    });

    // ==================== 用户子服务接口 ====================
    server.Post("/api/user/valid/nickname", [this](const httplib::Request& req, httplib::Response& res) {
        handleValidNickname(req, res);
    });
    server.Post("/api/user/valid/email", [this](const httplib::Request& req, httplib::Response& res) {
        handleValidEmail(req, res);
    });
    server.Post("/api/user/register", [this](const httplib::Request& req, httplib::Response& res) {
        handleUserRegister(req, res);
    });
    server.Post("/api/user/passwd/login", [this](const httplib::Request& req, httplib::Response& res) {
        handlePasswordLogin(req, res);
    });
    server.Post("/api/user/code", [this](const httplib::Request& req, httplib::Response& res) {
        handleGetVerifyCode(req, res);
    });
    server.Post("/api/user/vcode/login", [this](const httplib::Request& req, httplib::Response& res) {
        handleVerifyCodeLogin(req, res);
    });
    server.Post("/api/user/session/login", [this](const httplib::Request& req, httplib::Response& res) {
        handleSessionLogin(req, res);
    });
    server.Post("/api/user/logout", [this](const httplib::Request& req, httplib::Response& res) {
        handleLogout(req, res);
    });
    // 接口文档 2.2.9 定义为 POST（课件原版此处绑定为 server.Get，偏差记录见 CODING_SPEC.md 规则3）
    server.Post("/api/user/info", [this](const httplib::Request& req, httplib::Response& res) {
        handleGetUserInfo(req, res);
    });

    // ==================== 文件子服务接口 ====================
    server.Post("/api/file/upload/info", [this](const httplib::Request& req, httplib::Response& res) {
        handleFileUploadInfo(req, res);
    });
    server.Get("/api/file/info", [this](const httplib::Request& req, httplib::Response& res) {
        handleGetFileInfo(req, res);
    });
    server.Post("/api/file/upload", [this](const httplib::Request& req, httplib::Response& res) {
        handleFileUpload(req, res);
    });
    server.Get("/api/file/download", [this](const httplib::Request& req, httplib::Response& res) {
        handleFileDownload(req, res);
    });
    // fileId 为路径参数，用正则捕获：req.matches[1] 即 fileId
    server.Delete(R"(/api/file/([\w-]+))", [this](const httplib::Request& req, httplib::Response& res) {
        handleFileDelete(req, res);
    });
    server.Post("/api/file/preview", [this](const httplib::Request& req, httplib::Response& res) {
        handleFilePreview(req, res);
    });
    server.Post("/api/file/list", [this](const httplib::Request& req, httplib::Response& res) {
        handleGetFileList(req, res);
    });
    server.Post("/api/file/chat/map", [this](const httplib::Request& req, httplib::Response& res) {
        handleFileChatMap(req, res);
    });
    server.Post("/api/file/sqlite/upload", [this](const httplib::Request& req, httplib::Response& res) {
        handleSqliteUpload(req, res);
    });

    // ==================== 存储子服务接口 ====================
    server.Post("/api/db/connect", [this](const httplib::Request& req, httplib::Response& res) {
        handleDbConnect(req, res);
    });
    server.Post("/api/db/disconnect", [this](const httplib::Request& req, httplib::Response& res) {
        handleDbDisconnect(req, res);
    });
    server.Get("/api/db/tables", [this](const httplib::Request& req, httplib::Response& res) {
        handleGetDbTables(req, res);
    });
    server.Post("/api/db/table/data", [this](const httplib::Request& req, httplib::Response& res) {
        handleGetTableData(req, res);
    });
    server.Post("/api/db/connection/status", [this](const httplib::Request& req, httplib::Response& res) {
        handleGetConnectionStatus(req, res);
    });

    // ==================== AI子服务接口 ====================
    server.Post("/api/ai/models", [this](const httplib::Request& req, httplib::Response& res) {
        handleGetModels(req, res);
    });
    server.Post("/api/ai/session/create", [this](const httplib::Request& req, httplib::Response& res) {
        handleCreateChatSession(req, res);
    });
    server.Post("/api/ai/chatSessionLists", [this](const httplib::Request& req, httplib::Response& res) {
        handleGetChatSessionLists(req, res);
    });
    server.Post("/api/ai/history", [this](const httplib::Request& req, httplib::Response& res) {
        handleGetHistory(req, res);
    });
    server.Post("/api/ai/sendStreamMessage", [this](const httplib::Request& req, httplib::Response& res) {
        handleAiChat(req, res);
    });
    server.Post("/api/ai/delete", [this](const httplib::Request& req, httplib::Response& res) {
        handleDeleteChatSession(req, res);
    });

    INF("HTTP routes bound successfully, total 30 routes");
}

//////////////////////////////// 健康监测 ////////////////////////////////

// Response 对象的主要成员变量及作用（cpp-httplib）：
// | 成员 | 类型 | 作用 | 你的代码中的使用 |
// | :--- | :--- | :--- | :--- |
// | `status` | `int` | HTTP 状态码 (200, 404, 503等) | ✅ `res.status = statusCode;` |
// | `body` | `std::string` | 响应体内容 | 通过 `set_content()` 间接设置 |
// | `headers` | `Headers` (map) | 响应头键值对集合 | 通过 `set_content()` 自动设置 Content-Type |
// | `content_length_` | `size_t` | 响应体字节长度 | 由 `set_content()` 自动计算 |
// | `content_provider_` | 函数对象 | 流式/分块传输的内容提供器 | 未使用（当前为内存模式） |
// | `location` | `std::string` | 3xx 重定向目标 URL | 未使用 |
// | `reason` | `std::string` | 状态码对应的文本 ("OK", "Not Found") | 通常自动生成，可手动覆盖 |

//变更原因不同（单一职责原则）
// | 维度 | `sendJsonResponse` | `sendErrorResponse` |
// | :--- | :--- | :--- |
// | 为什么改它？ | HTTP 协议变了、要加压缩、要改 Content-Type、要支持 SSE/流式响应 | 错误格式变了、要加字段、要脱敏、要国际化 |
// | 改动频率 | 极低（基础设施稳定后几乎不动） | 较高（业务需求频繁调整错误契约） |
// | 影响范围 | 所有 JSON 接口（成功+失败） | 仅错误响应 |


//调用者不同（抽象层次不同）
// ┌─────────────────────────────────────────────┐
// │           业务 Handler / 中间件              │
// │                                             │
// │  ✅ 直接调用 sendErrorResponse(...)         │ ← 业务代码只关心"报什么错"
// │  ❌ 不应手动拼 JSON + 调 sendJsonResponse   │
// ├─────────────────────────────────────────────┤
// │           sendErrorResponse                 │ ← 业务语义层
// │  • 组装 requestId + errorCode + errorMsg    │
// │  • 知道"错误长什么样"                        │
// ├─────────────────────────────────────────────┤
// │           sendJsonResponse                  │ ← 传输协议层
// │  • set_content + status                     │
// │  • 不知道什么是"错误"，只知道"发JSON"          │
// └─────────────────────────────────────────────┘

// 类比理解
// 这就像写信和装信封的关系：
// sendErrorResponse = 写一封道歉信：决定措辞、格式、署名（业务语义）
// sendJsonResponse = 把信装进信封并贴邮票：决定信封大小、邮资、投递方式（传输协议）

void GatewayServiceImpl::sendJsonResponse(httplib::Response& res, const std::string& jsonData, int statusCode) {
    //第1行 - 设置响应体 + Content-Type：set_content 是 cpp-httplib 的 API，它同时完成两件事：
    // 将 jsonData 写入响应体，并将 Content-Type 头设为 application/json。这保证了客户端能正确解析 JSON。
    res.set_content(jsonData, "application/json");
    //第2行 - 设置状态码：
    // 直接赋值给 status 成员变量。cpp-httplib 的 Response::status 是公开字段，无需 setter。
    res.status = statusCode;
}

    // httplib::Response& res,       // 输出：HTTP响应容器
    // const std::string& requestId, // 输入：链路追踪ID
    // int errorCode,                // 输入：业务错误码
    // const std::string& errorMsg,  // 输入：人类可读的错误描述
    // int statusCode                // 输入：HTTP状态码
void GatewayServiceImpl::sendErrorResponse(httplib::Response& res,
                                          const std::string& requestId,
                                          int errorCode,
                                          const std::string& errorMsg,
                                          int statusCode) {
    // 用 jsoncpp 而非手拼字符串：errorMsg 是人为填充的中文，可能含引号/换行/特殊字符，
    // 库负责正确的转义与 UTF-8 处理
    Json::Value response;
    response["requestId"] = requestId;    // 字段名与接口文档严格一致，前端只需一个解析函数
    response["errorCode"] = errorCode;    // 收 int：JSON 中只有数字，handler 传入时做 static_cast<int>(ErrorCode::xxx)
    response["errorMsg"] = errorMsg;
    Json::StreamWriterBuilder writer;
    std::string jsonResponse = Json::writeString(writer, response);
    // 错误响应与成功响应走同一个 HTTP 出口，Content-Type / 状态码行为完全一致
    sendJsonResponse(res, jsonResponse, statusCode);
}


//对比：有 vs 没有 handleHealthCheck
// | 场景 | ❌ 没有健康检查端点 | ✅ 有 handleHealthCheck |
// | :--- | :--- | :--- |
// | 进程死锁 | 负载均衡器继续转发流量，用户大量 502 | K8s/Nginx 自动摘除，用户无感 |
// | 发布新版本 | 滚动更新期间旧Pod仍在接流，新Pod未就绪就接流 | Readiness 探针确保新Pod完全就绪后才接流 |
// | 排查问题 | 不知道服务是挂了还是慢了 | `timestamp` 字段可判断时钟漂移和响应延迟 |
// | 多服务共存 | 无法区分是哪个服务出了问题 | `service: "GatewayService"` 明确标识身份 |

// 四步流水线（日志→取时间→组装JSON→委托发送）
// 本阶段唯一真实现的业务接口：验证 编译→路由→httplib→JSON→日志 整条链路的最小闭环
void GatewayServiceImpl::handleHealthCheck(const httplib::Request& req, httplib::Response& res) {
    INF("Handling health check request");
    // 1. 生成当前时间戳（system_clock 精度为纳秒级，转换成秒以匹配接口文档格式）
        // system_clock::now() 的精度通常是纳秒或微秒，而接口文档要求秒级整数。这里用 duration_cast 而非隐式转换，有两个好处：
        // 意图明确：告诉阅读者"我故意丢弃了亚秒精度"，而非遗漏。
        // 类型安全：避免不同编译器对 count() 返回类型的隐式转换产生警告或未定义行为。
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
    // 2. 构建响应（字段名对齐接口文档 2.1）
    Json::Value response;
    response["status"] = "healthy";
    response["service"] = "GatewayService";
    response["timestamp"] = static_cast<Json::Int64>(timestamp);    // 显式转 Int64：rep 类型平台相关，秒级时间戳必须走 64 位
    // 3. 序列化响应为JSON字符串
    Json::StreamWriterBuilder writer;
    std::string jsonResponse = Json::writeString(writer, response);
    // 4. 发送JSON响应
    sendJsonResponse(res, jsonResponse, 200);
    INF("Health check response sent successfully");
}

////////////////////////////////// 公共方法（20 章 1 节）//////////////////////////////////

// 检测会话是否有效，在文件子服务中是通过获取文件信息，检测用户是否登录
// 即鉴权操作就是检测用户是否登录
bool GatewayServiceImpl::validateSession(const std::string& requestId,
                                         const std::string& sessionId, httplib::Response& res,
                                         std::string& userId) {
    // 1. 获取用户子服务的rpc通信信道
    auto channel = _svcChannels->getNode(FLAGS_user_service);
    if (!channel) {
        ERR("Failed to get UserService channel");
        sendErrorResponse(res, requestId, 503, "Service not available");
        return false;
    }
    // 2. 构建rpc请求
    chat2Data::userService::IsSessionValidRequest validRequest;
    validRequest.set_request_id(requestId);
    validRequest.set_session_id(sessionId);
    // 3. 创建用户子服务的rpc客户端
    chat2Data::userService::IsSessionValidResponse validResponse;
    brpc::Controller validController;
    chat2Data::userService::UserService_Stub stub(channel.get());
    // 4. 发起检测会话是否有效的rpc调用
    stub.IsSessionValid(&validController, &validRequest, &validResponse, nullptr);
    // 5. 检查rpc调用是否成功
    if (validController.Failed()) {
        ERR("IsSessionValid RPC failed: {}", validController.ErrorText());
        sendErrorResponse(res, requestId, 503, "Service not available");
        return false;
    }
    if (validResponse.error_code() != 0) {
        sendErrorResponse(res, validResponse.request_id(),
                          validResponse.error_code(), validResponse.error_msg());
        return false;
    }
    if (!validResponse.is_valid()) {
        sendErrorResponse(res, requestId, 401, "Session is invalid or expired");
        return false;
    }
    // 6. 通过引用参数带出userId
    userId = validResponse.user_id();
    // 7. 返回成功响应
    return true;
}

////////////////////////////////// 用户子服务接口 //////////////////////////////////

// 检测用户昵称是否唯一
void GatewayServiceImpl::handleValidNickname(const httplib::Request& req, httplib::Response& res) {
    INF("Handling valid nickname request");
    // 1. 反序列化请求体为JSON对象
    auto jsonOpt = biteutil::JSON::unserialize(req.body);
    if (!jsonOpt) {
        ERR("Failed to parse request body");
        sendErrorResponse(res, "", 400, "Invalid request body");
        return;
    }
    Json::Value requestJson = jsonOpt.value();
    // 2. 从JSON对象中提取请求参数
    std::string requestId = requestJson.get("requestId", "").asString();
    std::string nickname = requestJson.get("nickname", "").asString();
    // 3. 发送rpc调用
    // 3.1 获取UserService服务的channel
    auto channel = _svcChannels->getNode(FLAGS_user_service);
    if (!channel) {
        ERR("Failed to get UserService channel");
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    // 3.2 构建rpc请求
    chat2Data::userService::ValidNicknameRequest request;
    request.set_request_id(requestId);
    request.set_nickname(nickname);
    // 3.3 创建rpc的客户端
    chat2Data::userService::ValidNicknameResponse response;
    brpc::Controller controller;
    chat2Data::userService::UserService_Stub stub(channel.get());
    // 3.4 发起rpc调用
    stub.ValidNickname(&controller, &request, &response, nullptr);
    // 3.5 检测rpc调用是否成功
    if (controller.Failed()) {
        ERR("ValidNickname RPC failed: {}", controller.ErrorText());
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    if (response.error_code() != 0) {
        sendErrorResponse(res, response.request_id(), response.error_code(),
                          response.error_msg());
        return;
    }
    // 4 根据rpc响应构建HTTP响应，序列化成功之后将结果返回给前端
    Json::Value responseJson;
    responseJson["requestId"] = response.request_id();
    responseJson["errorCode"] = response.error_code();
    responseJson["errorMsg"] = response.error_msg();
    auto jsonStr = biteutil::JSON::serialize(responseJson);
    if (jsonStr) {
        sendJsonResponse(res, jsonStr.value());
    } else {
        sendErrorResponse(res, requestId, 500, "Internal server error");
    }
    INF("Valid nickname request handled successfully, requestId: {}", requestId);
}

// 检测用户邮箱是否唯一
void GatewayServiceImpl::handleValidEmail(const httplib::Request& req, httplib::Response& res) {
    INF("Handling valid email request");
    // 1. 反序列化请求体为JSON对象
    auto jsonOpt = biteutil::JSON::unserialize(req.body);
    if (!jsonOpt) {
        ERR("Failed to parse request body");
        sendErrorResponse(res, "", 400, "Invalid request body");
        return;
    }
    Json::Value requestJson = jsonOpt.value();
    // 2. 从JSON对象中提取请求参数
    std::string requestId = requestJson.get("requestId", "").asString();
    std::string email = requestJson.get("email", "").asString();
    // 3. 发送rpc调用
    // 3.1 获取UserService服务的channel
    auto channel = _svcChannels->getNode(FLAGS_user_service);
    if (!channel) {
        ERR("Failed to get UserService channel");
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    // 3.2 构建rpc请求
    chat2Data::userService::ValidEmailRequest request;
    request.set_request_id(requestId);
    request.set_email(email);
    // 3.3 创建rpc的客户端
    chat2Data::userService::ValidEmailResponse response;
    brpc::Controller controller;
    chat2Data::userService::UserService_Stub stub(channel.get());
    // 3.4 发起rpc调用
    stub.ValidEmail(&controller, &request, &response, nullptr);
    // 3.5 检测rpc调用是否成功
    if (controller.Failed()) {
        ERR("ValidEmail RPC failed: {}", controller.ErrorText());
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    if (response.error_code() != 0) {
        sendErrorResponse(res, response.request_id(), response.error_code(),
                          response.error_msg());
        return;
    }
    // 4 根据rpc响应构建HTTP响应，序列化成功之后将结果返回给前端
    Json::Value responseJson;
    responseJson["requestId"] = response.request_id();
    responseJson["errorCode"] = response.error_code();
    responseJson["errorMsg"] = response.error_msg();
    auto jsonStr = biteutil::JSON::serialize(responseJson);
    if (jsonStr) {
        sendJsonResponse(res, jsonStr.value());
    } else {
        sendErrorResponse(res, requestId, 500, "Internal server error");
    }
    INF("Valid email request handled successfully, requestId: {}", requestId);
}

// 用户注册
void GatewayServiceImpl::handleUserRegister(const httplib::Request& req, httplib::Response& res) {
    INF("Handling user register request");
    // 1. 反序列化请求体为JSON对象
    auto jsonOpt = biteutil::JSON::unserialize(req.body);
    if (!jsonOpt) {
        ERR("Failed to parse request body");
        sendErrorResponse(res, "", 400, "Invalid request body");
        return;
    }
    // 2. 从JSON对象中提取请求参数
    Json::Value requestJson = jsonOpt.value();
    std::string requestId = requestJson.get("requestId", "").asString();
    std::string nickname = requestJson.get("nickname", "").asString();
    std::string password = requestJson.get("password", "").asString();
    std::string email = requestJson.get("email", "").asString();
    // 3. 发送rpc调用
    // 3.1 获取UserService服务的channel
    auto channel = _svcChannels->getNode(FLAGS_user_service);
    if (!channel) {
        ERR("Failed to get UserService channel");
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    // 3.2 构建rpc请求
    chat2Data::userService::UserRegisterRequest request;
    request.set_request_id(requestId);
    request.set_nickname(nickname);
    request.set_password(password);
    request.set_email(email);
    // 3.3 创建rpc的客户端
    chat2Data::userService::UserRegisterResponse response;
    brpc::Controller controller;
    chat2Data::userService::UserService_Stub stub(channel.get());
    // 3.4 发起rpc调用
    stub.UserRegister(&controller, &request, &response, nullptr);
    // 3.5 检测rpc调用是否成功
    if (controller.Failed()) {
        ERR("UserRegister RPC failed: {}", controller.ErrorText());
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    if (response.error_code() != 0) {
        sendErrorResponse(res, response.request_id(), response.error_code(),
                          response.error_msg());
        return;
    }
    // 4 根据rpc响应构建HTTP响应，序列化成功之后将结果返回给前端
    Json::Value responseJson;
    responseJson["requestId"] = response.request_id();
    responseJson["errorCode"] = response.error_code();
    responseJson["errorMsg"] = response.error_msg();
    auto jsonStr = biteutil::JSON::serialize(responseJson);
    if (jsonStr) {
        sendJsonResponse(res, jsonStr.value());
    } else {
        sendErrorResponse(res, requestId, 500, "Internal server error");
    }
    INF("User register request handled successfully, requestId: {}", requestId);
}

// 会话登录
void GatewayServiceImpl::handleSessionLogin(const httplib::Request& req, httplib::Response& res) {
    INF("Handling session login request");
    // 1. 反序列化请求体为JSON对象
    auto jsonOpt = biteutil::JSON::unserialize(req.body);
    if (!jsonOpt) {
        ERR("Failed to parse request body");
        sendErrorResponse(res, "", 400, "Invalid request body");
        return;
    }
    // 2. 从JSON对象中提取请求参数
    Json::Value requestJson = jsonOpt.value();
    std::string requestId = requestJson.get("requestId", "").asString();
    std::string sessionId = requestJson.get("sessionId", "").asString();
    // 3. 发送rpc调用
    // 3.1 获取UserService服务的channel
    auto channel = _svcChannels->getNode(FLAGS_user_service);
    if (!channel) {
        ERR("Failed to get UserService channel");
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    // 3.2 构建rpc请求
    chat2Data::userService::SessionLoginRequest request;
    request.set_request_id(requestId);
    request.set_session_id(sessionId);
    // 3.3 创建rpc的客户端
    chat2Data::userService::SessionLoginResponse response;
    brpc::Controller controller;
    chat2Data::userService::UserService_Stub stub(channel.get());
    // 3.4 发起rpc调用
    stub.SessionLogin(&controller, &request, &response, nullptr);
    // 3.5 检测rpc调用是否成功
    if (controller.Failed()) {
        ERR("SessionLogin RPC failed: {}", controller.ErrorText());
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    if (response.error_code() != 0) {
        sendErrorResponse(res, response.request_id(), response.error_code(),
                          response.error_msg());
        return;
    }
    // 4 根据rpc响应构建HTTP响应，序列化成功之后将结果返回给前端
    Json::Value responseJson;
    responseJson["requestId"] = response.request_id();
    responseJson["errorCode"] = response.error_code();
    responseJson["errorMsg"] = response.error_msg();
    auto jsonStr = biteutil::JSON::serialize(responseJson);
    if (jsonStr) {
        sendJsonResponse(res, jsonStr.value());
    } else {
        sendErrorResponse(res, requestId, 500, "Internal server error");
    }
    INF("Session login request handled successfully, requestId: {}", requestId);
}

// 密码登录(用户名或邮箱)
void GatewayServiceImpl::handlePasswordLogin(const httplib::Request& req, httplib::Response& res) {
    INF("Handling password login request");
    // 1. 反序列化请求体为JSON对象
    auto jsonOpt = biteutil::JSON::unserialize(req.body);
    if (!jsonOpt) {
        ERR("Failed to parse request body");
        sendErrorResponse(res, "", 400, "Invalid request body");
        return;
    }
    // 2. 从JSON对象中提取请求参数
    Json::Value requestJson = jsonOpt.value();
    std::string requestId = requestJson.get("requestId", "").asString();
    std::string username = requestJson.get("username", "").asString();
    std::string password = requestJson.get("password", "").asString();
    // 3. 发送rpc调用
    // 3.1 获取UserService服务的channel
    auto channel = _svcChannels->getNode(FLAGS_user_service);
    if (!channel) {
        ERR("Failed to get UserService channel");
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    // 3.2 构建rpc请求
    chat2Data::userService::PasswdLoginRequest request;
    request.set_request_id(requestId);
    request.set_username(username);
    request.set_password(password);
    // 3.3 创建rpc的客户端
    chat2Data::userService::PasswdLoginResponse response;
    brpc::Controller controller;
    chat2Data::userService::UserService_Stub stub(channel.get());
    // 3.4 发起rpc调用
    stub.PasswdLogin(&controller, &request, &response, nullptr);
    // 3.5 检测rpc调用是否成功
    if (controller.Failed()) {
        ERR("PasswdLogin RPC failed: {}", controller.ErrorText());
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    if (response.error_code() != 0) {
        sendErrorResponse(res, response.request_id(), response.error_code(),
                          response.error_msg());
        return;
    }
    // 4 根据rpc响应构建HTTP响应，序列化成功之后将结果返回给前端
    Json::Value responseJson;
    responseJson["requestId"] = response.request_id();
    responseJson["errorCode"] = response.error_code();
    responseJson["errorMsg"] = response.error_msg();
    if (response.has_result()) {
        Json::Value resultJson;
        resultJson["sessionId"] = response.result().session_id();
        responseJson["result"] = resultJson;
    }
    auto jsonStr = biteutil::JSON::serialize(responseJson);
    if (jsonStr) {
        sendJsonResponse(res, jsonStr.value());
    } else {
        sendErrorResponse(res, requestId, 500, "Internal server error");
    }
    INF("Password login request handled successfully, requestId: {}", requestId);
}

// 获取验证码
void GatewayServiceImpl::handleGetVerifyCode(const httplib::Request& req, httplib::Response& res) {
    INF("Handling get verify code request");
    // 1. 反序列化请求体为JSON对象
    auto jsonOpt = biteutil::JSON::unserialize(req.body);
    if (!jsonOpt) {
        ERR("Failed to parse request body");
        sendErrorResponse(res, "", 400, "Invalid request body");
        return;
    }
    // 2. 从JSON对象中提取请求参数
    Json::Value requestJson = jsonOpt.value();
    std::string requestId = requestJson.get("requestId", "").asString();
    std::string email = requestJson.get("email", "").asString();
    // 3. 发送rpc调用
    // 3.1 获取UserService服务的channel
    auto channel = _svcChannels->getNode(FLAGS_user_service);
    if (!channel) {
        ERR("Failed to get UserService channel");
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    // 3.2 构建rpc请求
    chat2Data::userService::GetCodeRequest request;
    request.set_request_id(requestId);
    request.set_email(email);
    // 3.3 创建rpc的客户端
    chat2Data::userService::GetCodeResponse response;
    brpc::Controller controller;
    chat2Data::userService::UserService_Stub stub(channel.get());
    // 3.4 发起rpc调用
    stub.GetCode(&controller, &request, &response, nullptr);
    // 3.5 检测rpc调用是否成功
    if (controller.Failed()) {
        ERR("GetCode RPC failed: {}", controller.ErrorText());
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    if (response.error_code() != 0) {
        sendErrorResponse(res, response.request_id(), response.error_code(),
                          response.error_msg());
        return;
    }
    // 4 根据rpc响应构建HTTP响应，序列化成功之后将结果返回给前端
    Json::Value responseJson;
    responseJson["requestId"] = response.request_id();
    responseJson["errorCode"] = response.error_code();
    responseJson["errorMsg"] = response.error_msg();
    if (response.has_result()) {
        Json::Value resultJson;
        resultJson["codeId"] = response.result().code_id();
        responseJson["result"] = resultJson;
    }
    auto jsonStr = biteutil::JSON::serialize(responseJson);
    if (jsonStr) {
        sendJsonResponse(res, jsonStr.value());
    } else {
        sendErrorResponse(res, requestId, 500, "Internal server error");
    }
    INF("Get verify code request handled successfully, requestId: {}", requestId);
}

// 验证码登录
void GatewayServiceImpl::handleVerifyCodeLogin(const httplib::Request& req, httplib::Response& res) {
    INF("Handling verify code login request");
    // 1. 反序列化请求体为JSON对象
    auto jsonOpt = biteutil::JSON::unserialize(req.body);
    if (!jsonOpt) {
        ERR("Failed to parse request body");
        sendErrorResponse(res, "", 400, "Invalid request body");
        return;
    }
    // 2. 从JSON对象中提取请求参数
    Json::Value requestJson = jsonOpt.value();
    std::string requestId = requestJson.get("requestId", "").asString();
    std::string email = requestJson.get("email", "").asString();
    std::string verifyCode = requestJson.get("verifyCode", "").asString();
    std::string codeId = requestJson.get("codeId", "").asString();
    // 3. 发送rpc调用
    // 3.1 获取UserService服务的channel
    auto channel = _svcChannels->getNode(FLAGS_user_service);
    if (!channel) {
        ERR("Failed to get UserService channel");
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    // 3.2 构建rpc请求
    chat2Data::userService::VcodeLoginRequest request;
    request.set_request_id(requestId);
    request.set_email(email);
    request.set_verify_code(verifyCode);
    request.set_code_id(codeId);
    // 3.3 创建rpc的客户端
    chat2Data::userService::VcodeLoginResponse response;
    brpc::Controller controller;
    chat2Data::userService::UserService_Stub stub(channel.get());
    // 3.4 发起rpc调用
    stub.VcodeLogin(&controller, &request, &response, nullptr);
    // 3.5 检测rpc调用是否成功
    if (controller.Failed()) {
        ERR("VcodeLogin RPC failed: {}", controller.ErrorText());
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    if (response.error_code() != 0) {
        sendErrorResponse(res, response.request_id(), response.error_code(),
                          response.error_msg());
        return;
    }
    // 4 根据rpc响应构建HTTP响应，序列化成功之后将结果返回给前端
    Json::Value responseJson;
    responseJson["requestId"] = response.request_id();
    responseJson["errorCode"] = response.error_code();
    responseJson["errorMsg"] = response.error_msg();
    if (response.has_result()) {
        Json::Value resultJson;
        resultJson["sessionId"] = response.result().session_id();
        responseJson["result"] = resultJson;
    }
    auto jsonStr = biteutil::JSON::serialize(responseJson);
    if (jsonStr) {
        sendJsonResponse(res, jsonStr.value());
    } else {
        sendErrorResponse(res, requestId, 500, "Internal server error");
    }
    INF("Verify code login request handled successfully, requestId: {}", requestId);
}

// 退出登录
void GatewayServiceImpl::handleLogout(const httplib::Request& req, httplib::Response& res) {
    INF("Handling logout request");
    // 1. 反序列化请求体为JSON对象
    auto jsonOpt = biteutil::JSON::unserialize(req.body);
    if (!jsonOpt) {
        ERR("Failed to parse request body");
        sendErrorResponse(res, "", 400, "Invalid request body");
        return;
    }
    // 2. 从JSON对象中提取请求参数
    Json::Value requestJson = jsonOpt.value();
    std::string requestId = requestJson.get("requestId", "").asString();
    std::string sessionId = requestJson.get("sessionId", "").asString();
    // 3. 发送rpc调用
    // 3.1 获取UserService服务的channel
    auto channel = _svcChannels->getNode(FLAGS_user_service);
    if (!channel) {
        ERR("Failed to get UserService channel");
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    // 3.2 构建rpc请求
    chat2Data::userService::IsSessionValidRequest validRequest;
    validRequest.set_request_id(requestId);
    validRequest.set_session_id(sessionId);
    // 3.3 创建rpc的客户端
    chat2Data::userService::IsSessionValidResponse validResponse;
    brpc::Controller validController;
    chat2Data::userService::UserService_Stub stub(channel.get());
    // 3.4 鉴权
    stub.IsSessionValid(&validController, &validRequest, &validResponse, nullptr);
    if (validController.Failed()) {
        ERR("IsSessionValid RPC failed: {}", validController.ErrorText());
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    if (validResponse.error_code() != 0) {
        sendErrorResponse(res, validResponse.request_id(),
                          validResponse.error_code(), validResponse.error_msg());
        return;
    }
    if (!validResponse.is_valid()) {
        sendErrorResponse(res, requestId, 401, "Session is invalid or expired");
        return;
    }
    // 3.5 构建rpc请求
    chat2Data::userService::LogoutRequest request;
    request.set_request_id(requestId);
    request.set_session_id(sessionId);
    // 3.6 发送rpc调用
    chat2Data::userService::LogoutResponse response;
    brpc::Controller controller;
    stub.Logout(&controller, &request, &response, nullptr);
    // 3.7 检测rpc调用是否成功
    if (controller.Failed()) {
        ERR("Logout RPC failed: {}", controller.ErrorText());
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    if (response.error_code() != 0) {
        sendErrorResponse(res, response.request_id(), response.error_code(),
                          response.error_msg());
        return;
    }
    // 4. 根据rpc响应构建HTTP响应，序列化成功之后将结果返回给前端
    Json::Value responseJson;
    responseJson["requestId"] = response.request_id();
    responseJson["errorCode"] = response.error_code();
    responseJson["errorMsg"] = response.error_msg();
    auto jsonStr = biteutil::JSON::serialize(responseJson);
    if (jsonStr) {
        sendJsonResponse(res, jsonStr.value());
    } else {
        sendErrorResponse(res, requestId, 500, "Internal server error");
    }
    INF("Logout request handled successfully, requestId: {}", requestId);
}

// 获取用户信息
void GatewayServiceImpl::handleGetUserInfo(const httplib::Request& req, httplib::Response& res) {
    INF("Handling get user info request");
    // 1 从url中提取请求参数
    if (!req.has_param("requestId") || !req.has_param("sessionId")) {
        ERR("Missing required parameters");
        sendErrorResponse(res, "", 400, "Missing required parameters");
        return;
    }
    std::string requestId = req.get_param_value("requestId");
    std::string sessionId = req.get_param_value("sessionId");
    INF("RequestId: {}, SessionId: {}", requestId, sessionId);
    // 2 发送rpc调用
    // 2.1 获取UserService服务的channel
    auto channel = _svcChannels->getNode(FLAGS_user_service);
    if (!channel) {
        ERR("Failed to get UserService channel");
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    // 2.2 构建rpc请求
    chat2Data::userService::IsSessionValidRequest validRequest;
    validRequest.set_request_id(requestId);
    validRequest.set_session_id(sessionId);
    // 2.3 创建rpc的客户端
    chat2Data::userService::IsSessionValidResponse validResponse;
    brpc::Controller validController;
    chat2Data::userService::UserService_Stub stub(channel.get());
    // 2.4 鉴权
    stub.IsSessionValid(&validController, &validRequest, &validResponse, nullptr);
    // 2.5 检测rpc调用是否成功
    if (validController.Failed()) {
        ERR("IsSessionValid RPC failed: {}", validController.ErrorText());
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    if (validResponse.error_code() != 0) {
        sendErrorResponse(res, validResponse.request_id(),
                          validResponse.error_code(), validResponse.error_msg());
        return;
    }
    if (!validResponse.is_valid()) {
        sendErrorResponse(res, requestId, 401, "Session is invalid or expired");
        return;
    }
    // 2.6 构建rpc请求
    chat2Data::userService::GetUserInfoRequest request;
    request.set_request_id(requestId);
    request.set_session_id(sessionId);
    // 2.7 发送rpc调用
    chat2Data::userService::GetUserInfoResponse response;
    brpc::Controller controller;
    stub.GetUserInfo(&controller, &request, &response, nullptr);
    // 2.8 检测rpc调用是否成功
    if (controller.Failed()) {
        ERR("GetUserInfo RPC failed: {}", controller.ErrorText());
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    if (response.error_code() != 0) {
        sendErrorResponse(res, response.request_id(), response.error_code(),
                          response.error_msg());
        return;
    }
    // 4. 根据rpc响应构建HTTP响应，序列化成功之后将结果返回给前端
    Json::Value responseJson;
    responseJson["requestId"] = response.request_id();
    responseJson["errorCode"] = response.error_code();
    responseJson["errorMsg"] = response.error_msg();
    if (response.has_result()) {
        Json::Value resultJson;
        Json::Value userInfoJson;
        userInfoJson["userId"] = response.result().user_info().user_id();
        userInfoJson["nickname"] = response.result().user_info().nickname();
        userInfoJson["email"] = response.result().user_info().email();
        resultJson["userInfo"] = userInfoJson;
        responseJson["result"] = resultJson;
    }
    auto jsonStr = biteutil::JSON::serialize(responseJson);
    if (jsonStr) {
        sendJsonResponse(res, jsonStr.value());
    } else {
        sendErrorResponse(res, requestId, 500, "Internal server error");
    }
    INF("Get user info request handled successfully, requestId: {}", requestId);
}

////////////////////////////////// 文件子服务接口 //////////////////////////////////

//////////////////////////////////////////////////////////
// 【函数】handleFileUploadInfo —— 上传文件信息（两段式上传的第一段）
//
// 【前因后果 / 链路位置】
//   文件上传被课件拆成两段：本接口先收 JSON 元信息（文件名/大小/扩展名），
//   由文件子服务生成 fileId 返回给前端；前端再调 handleFileUpload 发第二段
//   二进制数据。为什么拆两段：元信息是结构化 JSON 便于扩展，数据是二进制流
//   走 attachment 通道，两种载荷共用一个 HTTP body 会互相污染。
//   上下游：前端文件管理页/上传对话框 → 本函数 → FileService.UploadFileInfo
//   → 内部生成 fileId 并落 tbl_fileInfo（此时文件尚无数据，处于"已登记未上传"态）。
//////////////////////////////////////////////////////////
void GatewayServiceImpl::handleFileUploadInfo(const httplib::Request& req, httplib::Response& res) {
    INF("Handling file upload info request");
    // step1: 解析 JSON body，提取链路追踪与会话参数
    auto jsonOpt = biteutil::JSON::unserialize(req.body);
    if (!jsonOpt) {
        ERR("Failed to parse request body");
        sendErrorResponse(res, "", 400, "Invalid request body");
        return;
    }
    Json::Value requestJson = jsonOpt.value();
    std::string requestId = requestJson.get("requestId", "").asString();
    std::string sessionId = requestJson.get("sessionId", "").asString();
    // step2: 鉴权——validateSession 通过引用带出 userId（后续所有文件接口同此模式）
    std::string userId;
    if (!validateSession(requestId, sessionId, res, userId)) {
        return;
    }
    // step3: 取文件子服务信道并构建 UploadFileInfo 请求（嵌套 FileInfo 消息）
    auto fileChannel = _svcChannels->getNode(FLAGS_file_service);
    if (!fileChannel) {
        ERR("Failed to get FileService channel");
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    chat2Data::fileService::UploadFileInfoRequest request;
    request.set_request_id(requestId);
    request.set_session_id(sessionId);
    request.set_user_id(userId);
    if (requestJson.isMember("fileInfo")) {
        auto& fileInfoJson = requestJson["fileInfo"];
        chat2Data::fileService::FileInfo* fileInfo = request.mutable_file_info();
        fileInfo->set_filename(fileInfoJson.get("filename", "").asString());
        fileInfo->set_file_size(fileInfoJson.get("fileSize", 0).asInt64());
        fileInfo->set_file_ext(fileInfoJson.get("fileExt", "").asString());
    }
    // step4: 发起 RPC 并按统一模板检测（Failed→503，业务码→透传）
    chat2Data::fileService::UploadFileInfoResponse response;
    brpc::Controller controller;
    chat2Data::fileService::FileService_Stub fileStub(fileChannel.get());
    fileStub.UploadFileInfo(&controller, &request, &response, nullptr);
    if (controller.Failed()) {
        ERR("UploadFileInfo RPC failed: {}", controller.ErrorText());
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    if (response.error_code() != 0) {
        sendErrorResponse(res, response.request_id(), response.error_code(),
                          response.error_msg());
        return;
    }
    // step5: 组装响应——fileId 是本接口的核心产物，前端凭它发起第二段上传
    Json::Value responseJson;
    responseJson["requestId"] = response.request_id();
    responseJson["errorCode"] = response.error_code();
    responseJson["errorMsg"] = response.error_msg();
    if (response.has_result()) {
        Json::Value resultJson;
        resultJson["fileId"] = response.result().file_id();
        responseJson["result"] = resultJson;
    }
    auto jsonStr = biteutil::JSON::serialize(responseJson);
    if (jsonStr) {
        sendJsonResponse(res, jsonStr.value());
    } else {
        sendErrorResponse(res, requestId, 500, "Internal server error");
    }
    INF("File upload info request handled successfully, requestId: {}", requestId);
}

/* 【使用示例】
 *   curl -X POST 'http://dev-env-service:9000/api/file/upload/info' \
 *        -H 'Content-Type: application/json' \
 *        -d '{"requestId":"req-f1","sessionId":"<登录返回的sessionId>",
 *             "fileInfo":{"filename":"销售数据.xlsx","fileSize":4581,"fileExt":".xlsx"}}'
 *   响应：{"requestId":"req-f1","errorCode":0,"errorMsg":"...","result":{"fileId":"78a1-fd4e84cc-0000"}}
 *   （拿到 fileId 后调 /api/file/upload 发第二段）
 */

// 获取文件信息
//////////////////////////////////////////////////////////
// 【函数】handleGetFileInfo —— 查询文件元信息
//
// 【前因后果 / 链路位置】
//   文件管理列表点击某个文件、或打开 Excel 预览前，前端先查这条文件的元数据
//   （大小/上传时间/扩展名）用于展示与校验。数据源是文件子服务内存缓存优先
//   （Cache-Aside，15 章），未命中回源 tbl_fileInfo。
//   上下游：前端 → 本函数（GET + query 参数）→ FileService.GetFileInfo。
//   注意本接口参数走 URL query 而非 JSON body——GET 语义下参数必须在 URL 上。
//////////////////////////////////////////////////////////
void GatewayServiceImpl::handleGetFileInfo(const httplib::Request& req, httplib::Response& res) {
    INF("Handling get file info request");
    // step1: 从 URL query 提取参数（GET 请求无 body）
    if (!req.has_param("requestId") || !req.has_param("sessionId") ||
        !req.has_param("fileId")) {
        ERR("Missing required parameters");
        sendErrorResponse(res, "", 400, "Missing required parameters");
        return;
    }
    std::string requestId = req.get_param_value("requestId");
    std::string sessionId = req.get_param_value("sessionId");
    std::string fileId = req.get_param_value("fileId");
    // step2: 鉴权
    std::string userId;
    if (!validateSession(requestId, sessionId, res, userId)) {
        return;
    }
    // step3: 取信道并构建请求（user_id 一并下发：文件子服务做归属校验）
    auto fileChannel = _svcChannels->getNode(FLAGS_file_service);
    if (!fileChannel) {
        ERR("Failed to get FileService channel");
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    chat2Data::fileService::GetFileInfoRequest request;
    request.set_request_id(requestId);
    request.set_session_id(sessionId);
    request.set_file_id(fileId);
    request.set_user_id(userId);
    // step4: 调用 + 检测（模板）
    chat2Data::fileService::GetFileInfoResponse response;
    brpc::Controller controller;
    chat2Data::fileService::FileService_Stub fileStub(fileChannel.get());
    fileStub.GetFileInfo(&controller, &request, &response, nullptr);
    if (controller.Failed()) {
        ERR("GetFileInfo RPC failed: {}", controller.ErrorText());
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    if (response.error_code() != 0) {
        sendErrorResponse(res, response.request_id(), response.error_code(),
                          response.error_msg());
        return;
    }
    // step5: 组装响应——时间戳显式转 Int64（proto uint64 直接给 jsoncpp 会截断/溢出）
    Json::Value responseJson;
    responseJson["requestId"] = response.request_id();
    responseJson["errorCode"] = response.error_code();
    responseJson["errorMsg"] = response.error_msg();
    if (response.has_result()) {
        Json::Value resultJson;
        resultJson["fileId"] = response.result().file_id();
        resultJson["fileName"] = response.result().file_name();
        resultJson["fileSize"] = static_cast<Json::Int64>(response.result().file_size());
        resultJson["uploadTime"] = static_cast<Json::Int64>(response.result().upload_time());
        resultJson["fileExt"] = response.result().file_ext();
        responseJson["result"] = resultJson;
    }
    auto jsonStr = biteutil::JSON::serialize(responseJson);
    if (jsonStr) {
        sendJsonResponse(res, jsonStr.value());
    } else {
        sendErrorResponse(res, requestId, 500, "Internal server error");
    }
    INF("Get file info request handled successfully, requestId: {}", requestId);
}

/* 【使用示例】
 *   curl 'http://dev-env-service:9000/api/file/info?requestId=req-f2&sessionId=<sessionId>&fileId=78a1-fd4e84cc-0000'
 *   响应：{"requestId":"req-f2","errorCode":0,...,"result":{"fileId":"78a1-...","fileName":"销售数据.xlsx",
 *          "fileSize":4581,"uploadTime":1789533000,"fileExt":".xlsx"}}
 */

// 上传文件数据
//////////////////////////////////////////////////////////
// 【函数】handleFileUpload —— 上传文件数据（两段式上传的第二段）
//
// 【前因后果 / 链路位置】
//   承接 handleFileUploadInfo 返回的 fileId，前端把文件字节流放进 HTTP body
//   发到本接口。网关对数据【零加工】——不 base64、不分割、不缓存，
//   req.body 原样 append 进 RPC request_attachment（brpc 的零拷贝通道），
//   与 e2eUploadTest 里 uploadCntl.request_attachment().append(fileData) 完全同构。
//   上下游：前端（body=原始文件字节）→ 本函数 → FileService.UploadFile
//   → 内部 FDFS 存储 + ExcelParser 解析 + DatabaseService 建表入库（15 章全链路，
//   19 章 excelChatE2e 实测打通）。
//   关键契约：Content-Type 由前端自定（网关不检查），body 就是纯文件字节——
//   multipart/form-data 不适用（那会混入边界符，文件子服务拿到的是脏数据）。
//////////////////////////////////////////////////////////
void GatewayServiceImpl::handleFileUpload(const httplib::Request& req, httplib::Response& res) {
    // 1. 提取请求参数
    if (!req.has_param("requestId") || !req.has_param("sessionId") ||
        !req.has_param("fileId")) {
        ERR("Missing required parameters");
        sendErrorResponse(res, "", 400, "Missing required parameters");
        return;
    }
    std::string requestId = req.get_param_value("requestId");
    std::string sessionId = req.get_param_value("sessionId");
    std::string fileId = req.get_param_value("fileId");
    // 2. 鉴权操作，通过validateSession方法检测会话是否有效，同时通过引用参数获取userId
    std::string userId;
    if (!validateSession(requestId, sessionId, res, userId)) {
        return;
    }
    // 3. 获取FileService服务的channel
    auto fileChannel = _svcChannels->getNode(FLAGS_file_service);
    if (!fileChannel) {
        ERR("Failed to get FileService channel");
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    // 4. 构建rpc请求
    chat2Data::fileService::UploadFileRequest request;
    request.set_request_id(requestId);
    request.set_session_id(sessionId);
    request.set_file_id(fileId);
    request.set_user_id(userId);
    // 5. 发起rpc调用，文件数据通过attachment传输
    chat2Data::fileService::UploadFileResponse response;
    brpc::Controller controller;
    controller.request_attachment().append(req.body);
    chat2Data::fileService::FileService_Stub fileStub(fileChannel.get());
    fileStub.UploadFile(&controller, &request, &response, nullptr);
    // 6. 检测rpc调用是否成功
    if (controller.Failed()) {
        ERR("UploadFile RPC failed: {}", controller.ErrorText());
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    if (response.error_code() != 0) {
        sendErrorResponse(res, response.request_id(), response.error_code(),
                          response.error_msg());
        return;
    }
    // 7. 构建HTTP响应
    Json::Value responseJson;
    responseJson["requestId"] = response.request_id();
    responseJson["errorCode"] = response.error_code();
    responseJson["errorMsg"] = response.error_msg();
    auto jsonStr = biteutil::JSON::serialize(responseJson);
    if (jsonStr) {
        sendJsonResponse(res, jsonStr.value());
    } else {
        sendErrorResponse(res, requestId, 500, "Internal server error");
    }
    INF("File upload request handled successfully, requestId: {}", requestId);
}

/* 【使用示例】（fileId 来自 handleFileUploadInfo 的返回）
 *   curl -X POST 'http://dev-env-service:9000/api/file/upload?requestId=req-f3&sessionId=<sessionId>&fileId=78a1-fd4e84cc-0000' \
 *        --data-binary @销售数据.xlsx
 *   响应：{"requestId":"req-f3","errorCode":0,"errorMsg":"..."}
 *   （此接口返回后，Excel 已完成 FDFS 存储→解析→建表入库全链路）
 */

// 下载文件
//////////////////////////////////////////////////////////
// 【函数】handleFileDownload —— 下载文件（二进制直通，全站唯一非 JSON 出口）
//
// 【前因后果 / 链路位置】
//   前端点击下载/预览原始文件时调用。链路是上传的反向：FileService 从 FDFS
//   取回文件字节放 response_attachment → 网关取出后以 octet-stream 回给前端。
//   为什么不走 JSON：文件是二进制，JSON 化会膨胀且需转义；octet-stream 让
//   浏览器按原始字节处理（触发下载或内嵌预览）。
//   契约：失败时仍可能返回 JSON 错误体（Content-Type 由 sendErrorResponse 决定），
//   前端需按 Content-Type 区分成功/失败。
//////////////////////////////////////////////////////////
void GatewayServiceImpl::handleFileDownload(const httplib::Request& req, httplib::Response& res) {
    // 1. 提取请求参数（query：requestId/sessionId/fileId）
    if (!req.has_param("requestId") || !req.has_param("sessionId") ||
        !req.has_param("fileId")) {
        ERR("Missing required parameters");
        sendErrorResponse(res, "", 400, "Missing required parameters");
        return;
    }
    std::string requestId = req.get_param_value("requestId");
    std::string sessionId = req.get_param_value("sessionId");
    std::string fileId = req.get_param_value("fileId");
    // 2. 鉴权
    std::string userId;
    if (!validateSession(requestId, sessionId, res, userId)) {
        return;
    }
    // 3. 取信道 + 4. 构建请求
    auto fileChannel = _svcChannels->getNode(FLAGS_file_service);
    if (!fileChannel) {
        ERR("Failed to get FileService channel");
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    chat2Data::fileService::DownloadFileRequest request;
    request.set_request_id(requestId);
    request.set_session_id(sessionId);
    request.set_file_id(fileId);
    request.set_user_id(userId);
    // 5. 发起rpc调用，文件数据通过attachment返回
    chat2Data::fileService::DownloadFileResponse response;
    brpc::Controller controller;
    chat2Data::fileService::FileService_Stub fileStub(fileChannel.get());
    fileStub.DownloadFile(&controller, &request, &response, nullptr);
    // 6. 检测rpc调用是否成功
    if (controller.Failed()) {
        ERR("DownloadFile RPC failed: {}", controller.ErrorText());
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    if (response.error_code() != 0) {
        sendErrorResponse(res, response.request_id(), response.error_code(),
                          response.error_msg());
        return;
    }
    // 7. 从attachment中获取文件数据并返回给HTTP客户端（★ 二进制直通，非 JSON）
    std::string fileData = controller.response_attachment().to_string();
    res.set_content(fileData, "application/octet-stream");
    INF("File download request handled successfully, requestId: {}", requestId);
}

/* 【使用示例】
 *   curl -o 销售数据.xlsx 'http://dev-env-service:9000/api/file/download?requestId=req-f4&sessionId=<sessionId>&fileId=78a1-fd4e84cc-0000'
 *   成功：响应体即原始 xlsx 字节（Content-Type: application/octet-stream）
 *   失败：响应体为 JSON 错误体（Content-Type: application/json）
 */

// 删除文件
//////////////////////////////////////////////////////////
// 【函数】handleFileDelete —— 删除文件（级联删表）
//
// 【前因后果 / 链路位置】
//   文件管理页的删除按钮。注意两点结构差异：
//   ① fileId 不在 query 也不在 body，而是 URL 路径段（DELETE /api/file/<fileId>），
//      由 ch11 注册的正则路由 /api/file/([\w-]+) 捕获，req.matches[1] 取出；
//   ② 删除在文件子服务内部是级联的：FDFS 删文件 → tbl_fileInfo 删行
//      → DatabaseService.DropTableExcel 删 worksheet 表 → tbl_chatSession 里
//      引用它的会话行被 FK 级联删除（H3 外键，chatSessionTest 实证）。
//   上下游：前端删除按钮 → 本函数 → FileService.DeleteFile（内部多服务联动）。
//////////////////////////////////////////////////////////
void GatewayServiceImpl::handleFileDelete(const httplib::Request& req, httplib::Response& res) {
    // 1. 提取请求参数
    if (!req.has_param("requestId") || !req.has_param("sessionId")) {
        ERR("Missing required parameters");
        sendErrorResponse(res, "", 400, "Missing required parameters");
        return;
    }
    std::string fileId = req.matches[1];    // ★ 路由正则捕获组：DELETE /api/file/<fileId>
    std::string requestId = req.get_param_value("requestId");
    std::string sessionId = req.get_param_value("sessionId");
    // 2. 鉴权
    std::string userId;
    if (!validateSession(requestId, sessionId, res, userId)) {
        return;
    }
    // 3. 取信道 → 4. 构建 DeleteFileRequest（带 user_id：子服务做归属校验，防越权删他人文件）
    auto fileChannel = _svcChannels->getNode(FLAGS_file_service);
    if (!fileChannel) {
        ERR("Failed to get FileService channel");
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    chat2Data::fileService::DeleteFileRequest request;
    request.set_request_id(requestId);
    request.set_session_id(sessionId);
    request.set_file_id(fileId);
    request.set_user_id(userId);
    // 5. 调用 → 6. 检测（模板）
    chat2Data::fileService::DeleteFileResponse response;
    brpc::Controller controller;
    chat2Data::fileService::FileService_Stub fileStub(fileChannel.get());
    fileStub.DeleteFile(&controller, &request, &response, nullptr);
    if (controller.Failed()) {
        ERR("DeleteFile RPC failed: {}", controller.ErrorText());
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    if (response.error_code() != 0) {
        sendErrorResponse(res, response.request_id(), response.error_code(),
                          response.error_msg());
        return;
    }
    // 7. 组 JSON 返回（无 result——删除动作无载荷）
    Json::Value responseJson;
    responseJson["requestId"] = response.request_id();
    responseJson["errorCode"] = response.error_code();
    responseJson["errorMsg"] = response.error_msg();
    auto jsonStr = biteutil::JSON::serialize(responseJson);
    if (jsonStr) {
        sendJsonResponse(res, jsonStr.value());
    } else {
        sendErrorResponse(res, requestId, 500, "Internal server error");
    }
    INF("File delete request handled successfully, requestId: {}, fileId: {}",
        requestId, fileId);
}

/* 【使用示例】
 *   curl -X DELETE 'http://dev-env-service:9000/api/file/78a1-fd4e84cc-0000?requestId=req-f5&sessionId=<sessionId>'
 *   响应：{"requestId":"req-f5","errorCode":0,"errorMsg":"..."}
 *   （返回后 FDFS 文件/tbl_fileInfo 行/worksheet 表/关联会话行全部消失）
 */

// 预览Excel文件
//////////////////////////////////////////////////////////
// 【函数】handleFilePreview —— 分页预览 Excel 内容
//
// 【前因后果 / 链路位置】
//   Excel 预览页的数据源。前端指定 fileId 与分页参数，文件子服务从解析缓存
//   （tbl_worksheet 关联的 MySQL 表）按页读回。响应是全站最深的 JSON 结构：
//   fileInfo → excelData.sheets[] → {分页元数据, columns[], data[][]}——
//   这个形状是前端表格组件（行列渲染 + 分页器）的直接数据源，字段名改动会
//   直接破坏前端渲染，属于前后端强契约。
//////////////////////////////////////////////////////////
void GatewayServiceImpl::handleFilePreview(const httplib::Request& req, httplib::Response& res) {
    // 1. 反序列化 JSON body（预览参数多且含可选分页，走 POST+JSON 而非 GET query）
    auto jsonOpt = biteutil::JSON::unserialize(req.body);
    if (!jsonOpt) {
        ERR("Failed to parse request body");
        sendErrorResponse(res, "", 400, "Invalid request body");
        return;
    }
    Json::Value requestJson = jsonOpt.value();
    std::string requestId = requestJson.get("requestId", "").asString();
    std::string sessionId = requestJson.get("sessionId", "").asString();
    std::string fileId = requestJson.get("fileId", "").asString();
    // 2. 鉴权
    std::string userId;
    if (!validateSession(requestId, sessionId, res, userId)) {
        return;
    }
    // 3. 取信道 → 4. 构建请求（分页参数可选：不传则文件子服务用默认页大小）
    auto fileChannel = _svcChannels->getNode(FLAGS_file_service);
    if (!fileChannel) {
        ERR("Failed to get FileService channel");
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    chat2Data::fileService::PreviewExcelRequest request;
    request.set_request_id(requestId);
    request.set_session_id(sessionId);
    request.set_file_id(fileId);
    request.set_user_id(userId);
    if (requestJson.isMember("pageNumber")) {
        request.set_page_number(requestJson["pageNumber"].asInt());
    }
    if (requestJson.isMember("pageSize")) {
        request.set_page_size(requestJson["pageSize"].asInt());
    }
    // 5. 调用 → 6. 检测（模板）
    chat2Data::fileService::PreviewExcelResponse response;
    brpc::Controller controller;
    chat2Data::fileService::FileService_Stub fileStub(fileChannel.get());
    fileStub.PreviewExcel(&controller, &request, &response, nullptr);
    if (controller.Failed()) {
        ERR("PreviewExcel RPC failed: {}", controller.ErrorText());
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    if (response.error_code() != 0) {
        sendErrorResponse(res, response.request_id(), response.error_code(),
                          response.error_msg());
        return;
    }
    // 7. 构建HTTP响应——三层组装：result → excelData.sheets[] → {分页元数据, columns, data}
    Json::Value responseJson;
    responseJson["requestId"] = response.request_id();
    responseJson["errorCode"] = response.error_code();
    responseJson["errorMsg"] = response.error_msg();
    if (response.has_result()) {
        Json::Value resultJson;
        resultJson["fileId"] = response.result().file_id();
        resultJson["fileName"] = response.result().file_name();
        resultJson["fileSize"] = static_cast<Json::Int64>(response.result().file_size());
        resultJson["fileExt"] = response.result().file_ext();
        Json::Value sheetsJson;
        for (int i = 0; i < response.result().excel_data().sheets_size(); ++i) {
            const auto& sheet = response.result().excel_data().sheets(i);
            Json::Value sheetJson;
            sheetJson["name"] = sheet.name();
            sheetJson["totalRows"] = sheet.total_rows();
            sheetJson["colCount"] = sheet.col_count();
            sheetJson["currentPage"] = sheet.current_page();
            sheetJson["totalPages"] = sheet.total_pages();
            sheetJson["pageSize"] = sheet.page_size();
            // 表头数组：前端表格的第一行
            Json::Value columnsJson;
            for (int k = 0; k < sheet.columns_size(); ++k) {
                columnsJson.append(sheet.columns(k));
            }
            sheetJson["columns"] = columnsJson;
            // 数据二维数组：data[j][k] = 第 j 行第 k 列单元格
            Json::Value dataJson;
            for (int j = 0; j < sheet.data_size(); ++j) {
                const auto& row = sheet.data(j);
                Json::Value rowJson;
                for (int k = 0; k < row.cells_size(); ++k) {
                    rowJson.append(row.cells(k));
                }
                dataJson.append(rowJson);
            }
            sheetJson["data"] = dataJson;
            sheetsJson.append(sheetJson);
        }
        resultJson["excelData"]["sheets"] = sheetsJson;
        responseJson["result"] = resultJson;
    }
    auto jsonStr = biteutil::JSON::serialize(responseJson);
    if (jsonStr) {
        sendJsonResponse(res, jsonStr.value());
    } else {
        sendErrorResponse(res, requestId, 500, "Internal server error");
    }
    INF("File preview request handled successfully, requestId: {}", requestId);
}

/* 【使用示例】
 *   curl -X POST 'http://dev-env-service:9000/api/file/preview' \
 *        -H 'Content-Type: application/json' \
 *        -d '{"requestId":"req-f6","sessionId":"<sessionId>","fileId":"78a1-fd4e84cc-0000",
 *             "pageNumber":1,"pageSize":10}'
 *   响应 result 形态：
 *   {"fileId":"...","fileName":"销售数据.xlsx","fileSize":4581,"fileExt":".xlsx",
 *    "excelData":{"sheets":[{"name":"Sheet1","totalRows":6,"colCount":2,"currentPage":1,
 *      "totalPages":1,"pageSize":10,"columns":["客户名称","金额"],"data":[["张三","1234.56"],...]}]}}
 */

// 获取文件列表
//////////////////////////////////////////////////////////
// 【函数】handleGetFileList —— 用户文件列表
//
// 【前因后果 / 链路位置】
//   文件管理页的首屏接口。按 userId 拉全量文件（课程项目不做分页，量级可控），
//   每项带 chatSessionId——前端据此把文件与它的智能会话互相跳转（19 章双向关联
//   的读取侧）。数据源：文件子服务直查 tbl_fileInfo（列表无缓存——与 19 章
//   会话列表同理，单键缓存无法按用户聚合）。
//////////////////////////////////////////////////////////
void GatewayServiceImpl::handleGetFileList(const httplib::Request& req, httplib::Response& res) {
    // 1. 解析 JSON body
    auto jsonOpt = biteutil::JSON::unserialize(req.body);
    if (!jsonOpt) {
        ERR("Failed to parse request body");
        sendErrorResponse(res, "", 400, "Invalid request body");
        return;
    }
    Json::Value requestJson = jsonOpt.value();
    std::string requestId = requestJson.get("requestId", "").asString();
    std::string sessionId = requestJson.get("sessionId", "").asString();
    // 2. 鉴权（userId 即列表的过滤键）
    std::string userId;
    if (!validateSession(requestId, sessionId, res, userId)) {
        return;
    }
    // 3. 取信道 → 4. 构建 GetFileListRequest
    auto fileChannel = _svcChannels->getNode(FLAGS_file_service);
    if (!fileChannel) {
        ERR("Failed to get FileService channel");
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    chat2Data::fileService::GetFileListRequest request;
    request.set_request_id(requestId);
    request.set_session_id(sessionId);
    request.set_user_id(userId);
    // 5. 调用 → 6. 检测（模板）
    chat2Data::fileService::GetFileListResponse response;
    brpc::Controller controller;
    chat2Data::fileService::FileService_Stub fileStub(fileChannel.get());
    fileStub.GetFileList(&controller, &request, &response, nullptr);
    if (controller.Failed()) {
        ERR("GetFileList RPC failed: {}", controller.ErrorText());
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    if (response.error_code() != 0) {
        sendErrorResponse(res, response.request_id(), response.error_code(),
                          response.error_msg());
        return;
    }
    // 7. 组装响应：fileList 数组，逐项平铺（含 chatSessionId 供前端跳转）
    Json::Value responseJson;
    responseJson["requestId"] = response.request_id();
    responseJson["errorCode"] = response.error_code();
    responseJson["errorMsg"] = response.error_msg();
    if (response.has_result()) {
        Json::Value fileListJson;
        for (int i = 0; i < response.result().file_list_size(); ++i) {
            const auto& fileItem = response.result().file_list(i);
            Json::Value itemJson;
            itemJson["fileId"] = fileItem.file_id();
            itemJson["fileName"] = fileItem.file_name();
            itemJson["fileSize"] = static_cast<Json::Int64>(fileItem.file_size());
            itemJson["uploadTime"] = static_cast<Json::Int64>(fileItem.upload_time());
            itemJson["chatSessionId"] = fileItem.chat_session_id();
            fileListJson.append(itemJson);
        }
        responseJson["result"]["fileList"] = fileListJson;
    }
    auto jsonStr = biteutil::JSON::serialize(responseJson);
    if (jsonStr) {
        sendJsonResponse(res, jsonStr.value());
    } else {
        sendErrorResponse(res, requestId, 500, "Internal server error");
    }
    INF("Get file list request handled successfully, requestId: {}", requestId);
}

/* 【使用示例】
 *   curl -X POST 'http://dev-env-service:9000/api/file/list' \
 *        -H 'Content-Type: application/json' \
 *        -d '{"requestId":"req-f7","sessionId":"<sessionId>"}'
 *   响应 result：{"fileList":[{"fileId":"78a1-...","fileName":"销售数据.xlsx",
 *                "fileSize":4581,"uploadTime":1789533000,"chatSessionId":"session_..."}]}
 */

// 关联文件和聊天会话
//////////////////////////////////////////////////////////
// 【函数】handleFileChatMap —— 文件 ↔ 聊天会话双向关联入口
//
// 【前因后果 / 链路位置】
//   前端在 AI 会话里"挂载"某个已上传文件时调用。这是 19 章 H14 链路的网关侧
//   入口：一次请求触发文件子服务两侧落账——①文件侧写 tbl_fileInfo.chatSessionId；
//   ②文件服务再 RPC 调 AI 子服务 UpdateSessionFile，把 fileId 记到 tbl_chatSession
//   （AI 侧做归属校验）。两处都落好，SendMessage(excel) 才能凭 fileId 找到
//   worksheet 表名（getExcelWorksheetTables）凭表名生成 SQL。
//////////////////////////////////////////////////////////
void GatewayServiceImpl::handleFileChatMap(const httplib::Request& req, httplib::Response& res) {
    // 1. 解析 JSON body
    auto jsonOpt = biteutil::JSON::unserialize(req.body);
    if (!jsonOpt) {
        ERR("Failed to parse request body");
        sendErrorResponse(res, "", 400, "Invalid request body");
        return;
    }
    Json::Value requestJson = jsonOpt.value();
    std::string requestId = requestJson.get("requestId", "").asString();
    std::string sessionId = requestJson.get("sessionId", "").asString();
    std::string fileId = requestJson.get("fileId", "").asString();
    std::string chatSessionId = requestJson.get("chatSessionId", "").asString();
    // 2. 鉴权
    std::string userId;
    if (!validateSession(requestId, sessionId, res, userId)) {
        return;
    }
    // 3. 取信道 → 4. 构建请求（四元组齐备：子服务内部两侧落账）
    auto fileChannel = _svcChannels->getNode(FLAGS_file_service);
    if (!fileChannel) {
        ERR("Failed to get FileService channel");
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    chat2Data::fileService::HandleFileChatSessionMapRequest request;
    request.set_request_id(requestId);
    request.set_session_id(sessionId);
    request.set_file_id(fileId);
    request.set_chat_session_id(chatSessionId);
    request.set_user_id(userId);
    // 5. 调用 → 6. 检测（AI 侧业务错误码原样透传：如 AI_SESSION_NOT_FOUND）
    chat2Data::fileService::HandleFileChatSessionMapResponse response;
    brpc::Controller controller;
    chat2Data::fileService::FileService_Stub fileStub(fileChannel.get());
    fileStub.HandleFileChatSessionMap(&controller, &request, &response, nullptr);
    if (controller.Failed()) {
        ERR("HandleFileChatSessionMap RPC failed: {}", controller.ErrorText());
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    if (response.error_code() != 0) {
        sendErrorResponse(res, response.request_id(), response.error_code(),
                          response.error_msg());
        return;
    }
    // 7. 组 JSON 返回（无 result）
    Json::Value responseJson;
    responseJson["requestId"] = response.request_id();
    responseJson["errorCode"] = response.error_code();
    responseJson["errorMsg"] = response.error_msg();
    auto jsonStr = biteutil::JSON::serialize(responseJson);
    if (jsonStr) {
        sendJsonResponse(res, jsonStr.value());
    } else {
        sendErrorResponse(res, requestId, 500, "Internal server error");
    }
    INF("File chat map request handled successfully, requestId: {}", requestId);
}

/* 【使用示例】
 *   curl -X POST 'http://dev-env-service:9000/api/file/chat/map' \
 *        -H 'Content-Type: application/json' \
 *        -d '{"requestId":"req-f8","sessionId":"<sessionId>",
 *             "fileId":"78a1-fd4e84cc-0000","chatSessionId":"session_1789390817_00000001"}'
 *   响应：{"requestId":"req-f8","errorCode":0,"errorMsg":"..."}
 *   （返回后该会话即具备 excel 智能对话能力）
 */

// 上传SQLite文件
//////////////////////////////////////////////////////////
// 【函数】handleSqliteUpload —— 上传 SQLite 数据库文件（一段式）
//
// 【前因后果 / 链路位置】
//   与 xlsx 两段式不同：SQLite 一段搞定——元信息在 query 参数（filename），
//   数据在 body，fileId 由服务端生成后返回。为什么不同：SQLite 必须收全整个
//   文件才能开库解析表结构，"先登记后传数据"的两段式没有意义。
//   链路：前端（body=整个 .db/.sqlite 文件字节）→ 本函数 → FileService.
//   UploadSQLiteFile → FDFS 存储 + 直接开库解析 + DatabaseService 建表入库。
//////////////////////////////////////////////////////////
void GatewayServiceImpl::handleSqliteUpload(const httplib::Request& req, httplib::Response& res) {
    // 1. 提取 query 参数（filename 用于落 tbl_fileInfo 与解析日志）
    if (!req.has_param("requestId") || !req.has_param("sessionId") ||
        !req.has_param("filename")) {
        ERR("Missing required parameters");
        sendErrorResponse(res, "", 400, "Missing required parameters");
        return;
    }
    std::string requestId = req.get_param_value("requestId");
    std::string sessionId = req.get_param_value("sessionId");
    std::string filename = req.get_param_value("filename");
    // 2. 鉴权
    std::string userId;
    if (!validateSession(requestId, sessionId, res, userId)) {
        return;
    }
    // 3. 取信道 → 4. 构建请求（filename + user_id；fileId 服务端生成）
    auto fileChannel = _svcChannels->getNode(FLAGS_file_service);
    if (!fileChannel) {
        ERR("Failed to get FileService channel");
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    chat2Data::fileService::UploadSQLiteFileRequest request;
    request.set_request_id(requestId);
    request.set_session_id(sessionId);
    request.set_filename(filename);
    request.set_user_id(userId);
    // 5. 发起rpc调用（body → attachment，与 handleFileUpload 同机制）
    chat2Data::fileService::UploadSQLiteFileResponse response;
    brpc::Controller controller;
    controller.request_attachment().append(req.body);
    chat2Data::fileService::FileService_Stub fileStub(fileChannel.get());
    fileStub.UploadSQLiteFile(&controller, &request, &response, nullptr);
    // 6. 检测（模板）
    if (controller.Failed()) {
        ERR("UploadSQLiteFile RPC failed: {}", controller.ErrorText());
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    if (response.error_code() != 0) {
        sendErrorResponse(res, response.request_id(), response.error_code(),
                          response.error_msg());
        return;
    }
    // 7. 组响应（fileId 由服务端生成——前端没有预先登记步骤，靠这里拿）
    Json::Value responseJson;
    responseJson["requestId"] = response.request_id();
    responseJson["errorCode"] = response.error_code();
    responseJson["errorMsg"] = response.error_msg();
    if (response.has_result()) {
        Json::Value resultJson;
        resultJson["fileId"] = response.result().file_id();
        responseJson["result"] = resultJson;
    }
    auto jsonStr = biteutil::JSON::serialize(responseJson);
    if (jsonStr) {
        sendJsonResponse(res, jsonStr.value());
    } else {
        sendErrorResponse(res, requestId, 500, "Internal server error");
    }
    INF("Sqlite upload request handled successfully, requestId: {}", requestId);
}

/* 【使用示例】
 *   curl -X POST 'http://dev-env-service:9000/api/file/sqlite/upload?requestId=req-f9&sessionId=<sessionId>&filename=资料库.db' \
 *        --data-binary @资料库.db
 *   响应：{"requestId":"req-f9","errorCode":0,...,"result":{"fileId":"a1b2-c3d4..."}}
 *   （返回后库内所有表已入库，可走 database 场景的智能对话）
 */

////////////////////////////////// 数据库子服务接口 //////////////////////////////////

//////////////////////////////////////////////////////////
// 【函数】handleDbConnect —— 连接外部数据库（MySQL/SQLite）
//
// 【前因后果 / 链路位置】
//   database 场景智能对话的前置步骤：前端在连接配置页填好数据库信息，
//   由网关转给数据库子服务建立连接并返回 connectionId——之后所有 SQL 执行、
//   表数据查看都凭这个 id。这是全项目"用户自带数据源"能力的入口。
//   关键转换：前端传类型字符串 "MYSQL"/"SQLITE"，网关负责转成 proto 枚举
//   （DATABASE_TYPE_*）；MySQL/SQLite 两套连接参数是 proto 的嵌套结构，
//   按前端给的类型分支填充。
//   上下游：前端连接配置页 → 本函数 → DatabaseService.ConnectDatabase
//   → 内部建 dbConnMgr 连接（17 章，MySQL 直连/SQLite 经 FastDFS 取回文件）。
//////////////////////////////////////////////////////////
void GatewayServiceImpl::handleDbConnect(const httplib::Request& req, httplib::Response& res) {
    INF("Handling db connect request");
    // step1: 解析 JSON body
    auto jsonOpt = biteutil::JSON::unserialize(req.body);
    if (!jsonOpt) {
        ERR("Failed to parse request body");
        sendErrorResponse(res, "", 400, "Invalid request body");
        return;
    }
    // step2: 提取参数（database 为嵌套对象：type + MySQL/SQLite 二选一的配置块）
    Json::Value requestJson = jsonOpt.value();
    std::string requestId = requestJson.get("requestId", "").asString();
    std::string sessionId = requestJson.get("sessionId", "").asString();
    // step3: 鉴权
    std::string userId;
    if (!validateSession(requestId, sessionId, res, userId)) {
        return;
    }
    // step4: 取 DatabaseService 信道
    auto dbChannel = _svcChannels->getNode(FLAGS_db_service);
    if (!dbChannel) {
        ERR("Failed to get DatabaseService channel");
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    // step5: 构建请求——类型字符串统一转大写再映射枚举（容忍前端传 "mysql"/"MySQL"）；
    //        MySQL/SQLite 配置块按类型分支填充，charset 缺省 utf8mb4
    chat2Data::DatabaseService::ConnectDatabaseRequest request;
    request.set_request_id(requestId);
    request.set_session_id(sessionId);
    request.set_user_id(userId);
    if (requestJson.isMember("database")) {
        auto& dbJson = requestJson["database"];
        auto* database = request.mutable_database();
        if (dbJson.isMember("type")) {
            std::string typeStr = dbJson["type"].asString();
            std::transform(typeStr.begin(), typeStr.end(), typeStr.begin(), ::toupper);
            if (typeStr == "MYSQL") {
                database->set_type(chat2Data::DatabaseService::DATABASE_TYPE_MYSQL);
            } else if (typeStr == "SQLITE") {
                database->set_type(chat2Data::DatabaseService::DATABASE_TYPE_SQLITE);
            } else {
                database->set_type(chat2Data::DatabaseService::DATABASE_TYPE_UNKNOWN);
            }
        }
        if (dbJson.isMember("MySQL") && dbJson["MySQL"].isObject()) {
            auto& mysqlJson = dbJson["MySQL"];
            auto* mysqlConfig = database->mutable_mysql_config();
            mysqlConfig->set_host(mysqlJson.get("host", "").asString());
            mysqlConfig->set_port(mysqlJson.get("port", 0).asInt());
            mysqlConfig->set_name(mysqlJson.get("name", "").asString());
            mysqlConfig->set_username(mysqlJson.get("username", "").asString());
            mysqlConfig->set_password(mysqlJson.get("password", "").asString());
            mysqlConfig->set_charset(mysqlJson.get("charset", "utf8mb4").asString());
        }
        if (dbJson.isMember("SQLite") && dbJson["SQLite"].isObject()) {
            auto& sqliteJson = dbJson["SQLite"];
            auto* sqliteConfig = database->mutable_sqlite_config();
            sqliteConfig->set_file_id(sqliteJson.get("fileId", "").asString());
            sqliteConfig->set_readonly(sqliteJson.get("readonly", false).asBool());
        }
    }
    // step6: 调用 + 检测（模板）
    chat2Data::DatabaseService::ConnectDatabaseResponse response;
    brpc::Controller controller;
    chat2Data::DatabaseService::DatabaseService_Stub dbStub(dbChannel.get());
    dbStub.ConnectDatabase(&controller, &request, &response, nullptr);
    if (controller.Failed()) {
        ERR("ConnectDatabase RPC failed: {}", controller.ErrorText());
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    if (response.error_code() != 0) {
        sendErrorResponse(res, response.request_id(), response.error_code(),
                          response.error_msg());
        return;
    }
    // step7: 组响应——connectionId 是后续所有 SQL/表操作的凭据
    Json::Value responseJson;
    responseJson["requestId"] = response.request_id();
    responseJson["errorCode"] = response.error_code();
    responseJson["errorMsg"] = response.error_msg();
    if (response.has_result()) {
        Json::Value resultJson;
        resultJson["connectionId"] = response.result().connection_id();
        responseJson["result"] = resultJson;
    }
    auto jsonStr = biteutil::JSON::serialize(responseJson);
    if (jsonStr) {
        sendJsonResponse(res, jsonStr.value());
    } else {
        sendErrorResponse(res, requestId, 500, "Internal server error");
    }
    INF("Db connect request handled successfully, requestId: {}", requestId);
}

/* 【使用示例】
 *   curl -X POST 'http://dev-env-service:9000/api/db/connect' \
 *        -H 'Content-Type: application/json' \
 *        -d '{"requestId":"req-d1","sessionId":"<sessionId>",
 *             "database":{"type":"MySQL","MySQL":{"host":"dev-mysql","port":3306,
 *             "name":"chat2Data","username":"root","password":"123456"}}}'
 *   响应：{"requestId":"req-d1","errorCode":0,...,"result":{"connectionId":"conn_xxx"}}
 */

// 断开数据库连接
//////////////////////////////////////////////////////////
// 【函数】handleDbDisconnect —— 断开外部数据库连接
//
// 【前因后果 / 链路位置】
//   handleDbConnect 的逆操作：前端关闭连接配置页/切换数据源时调用。
//   子服务内部（17 章 disconnect）会清理连接对象并**删除该连接的全部临时表**——
//   所以断开前若用户还想查看沙箱态数据，应先调 handleGetConnectionStatus。
//////////////////////////////////////////////////////////
void GatewayServiceImpl::handleDbDisconnect(const httplib::Request& req, httplib::Response& res) {
    INF("Handling db disconnect request");
    // step1: 解析 JSON body
    auto jsonOpt = biteutil::JSON::unserialize(req.body);
    if (!jsonOpt) {
        ERR("Failed to parse request body");
        sendErrorResponse(res, "", 400, "Invalid request body");
        return;
    }
    // step2: 提取参数
    Json::Value requestJson = jsonOpt.value();
    std::string requestId = requestJson.get("requestId", "").asString();
    std::string sessionId = requestJson.get("sessionId", "").asString();
    std::string connectionId = requestJson.get("connectionId", "").asString();
    // step3: 鉴权
    std::string userId;
    if (!validateSession(requestId, sessionId, res, userId)) {
        return;
    }
    // step4: 取信道 → 5. 构建请求
    auto dbChannel = _svcChannels->getNode(FLAGS_db_service);
    if (!dbChannel) {
        ERR("Failed to get DatabaseService channel");
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    chat2Data::DatabaseService::DisconnectDatabaseRequest request;
    request.set_request_id(requestId);
    request.set_session_id(sessionId);
    request.set_connection_id(connectionId);
    // step6: 调用 + 检测（模板）
    chat2Data::DatabaseService::DisconnectDatabaseResponse response;
    brpc::Controller controller;
    chat2Data::DatabaseService::DatabaseService_Stub dbStub(dbChannel.get());
    dbStub.DisconnectDatabase(&controller, &request, &response, nullptr);
    if (controller.Failed()) {
        ERR("DisconnectDatabase RPC failed: {}", controller.ErrorText());
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    if (response.error_code() != 0) {
        sendErrorResponse(res, response.request_id(), response.error_code(),
                          response.error_msg());
        return;
    }
    // step7: 组 JSON 返回（无 result）
    Json::Value responseJson;
    responseJson["requestId"] = response.request_id();
    responseJson["errorCode"] = response.error_code();
    responseJson["errorMsg"] = response.error_msg();
    auto jsonStr = biteutil::JSON::serialize(responseJson);
    if (jsonStr) {
        sendJsonResponse(res, jsonStr.value());
    } else {
        sendErrorResponse(res, requestId, 500, "Internal server error");
    }
    INF("Db disconnect request handled successfully, requestId: {}", requestId);
}

/* 【使用示例】
 *   curl -X POST 'http://dev-env-service:9000/api/db/disconnect' \
 *        -H 'Content-Type: application/json' \
 *        -d '{"requestId":"req-d2","sessionId":"<sessionId>","connectionId":"conn_xxx"}'
 *   响应：{"requestId":"req-d2","errorCode":0,"errorMsg":"..."}
 *   （返回后该连接的临时表已全部删除）
 */

// 获取数据库表列表
//////////////////////////////////////////////////////////
// 【函数】handleGetDbTables —— 列出连接下的全部表名
//
// 【前因后果 / 链路位置】
//   database 场景的"数据表页"首屏：用户连上库后先看有哪些表，勾选要对话的表
//   （表名经 handleAiChat 的 tableName 参数传给 AI 子服务做上下文）。
//   请求参数走 URL query（GET 语义）。数据源：数据库子服务 ListTables——
//   MySQL 走 SHOW TABLES，SQLite 走 sqlite_master。
//////////////////////////////////////////////////////////
void GatewayServiceImpl::handleGetDbTables(const httplib::Request& req, httplib::Response& res) {
    INF("Handling get db tables request");
    // step1: 从 URL query 提取参数（GET 请求）
    if (!req.has_param("requestId") || !req.has_param("sessionId") ||
        !req.has_param("dbConnectId")) {
        ERR("Missing required parameters");
        sendErrorResponse(res, "", 400, "Missing required parameters");
        return;
    }
    std::string requestId = req.get_param_value("requestId");
    std::string sessionId = req.get_param_value("sessionId");
    std::string dbConnectId = req.get_param_value("dbConnectId");
    // step2: 鉴权
    std::string userId;
    if (!validateSession(requestId, sessionId, res, userId)) {
        return;
    }
    // step3: 取信道 → 4. 构建 ListTablesRequest
    auto dbChannel = _svcChannels->getNode(FLAGS_db_service);
    if (!dbChannel) {
        ERR("Failed to get DatabaseService channel");
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    chat2Data::DatabaseService::ListTablesRequest request;
    request.set_request_id(requestId);
    request.set_session_id(sessionId);
    request.set_db_connect_id(dbConnectId);
    // step5: 调用 + 检测（模板）
    chat2Data::DatabaseService::ListTablesResponse response;
    brpc::Controller controller;
    chat2Data::DatabaseService::DatabaseService_Stub dbStub(dbChannel.get());
    dbStub.ListTables(&controller, &request, &response, nullptr);
    if (controller.Failed()) {
        ERR("ListTables RPC failed: {}", controller.ErrorText());
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    if (response.error_code() != 0) {
        sendErrorResponse(res, response.request_id(), response.error_code(),
                          response.error_msg());
        return;
    }
    // step6: 组响应——tables 字符串数组直出
    Json::Value responseJson;
    responseJson["requestId"] = response.request_id();
    responseJson["errorCode"] = response.error_code();
    responseJson["errorMsg"] = response.error_msg();
    if (response.has_result()) {
        Json::Value resultJson;
        Json::Value tablesJson;
        for (int i = 0; i < response.result().tables_size(); ++i) {
            tablesJson.append(response.result().tables(i));
        }
        resultJson["tables"] = tablesJson;
        responseJson["result"] = resultJson;
    }
    auto jsonStr = biteutil::JSON::serialize(responseJson);
    if (jsonStr) {
        sendJsonResponse(res, jsonStr.value());
    } else {
        sendErrorResponse(res, requestId, 500, "Internal server error");
    }
    INF("Get db tables request handled successfully, requestId: {}", requestId);
}

/* 【使用示例】
 *   curl 'http://dev-env-service:9000/api/db/tables?requestId=req-d3&sessionId=<sessionId>&dbConnectId=conn_xxx'
 *   响应：{"requestId":"req-d3","errorCode":0,...,"result":{"tables":["Sheet1_xxx","员工信息表"]}}
 */

// 获取数据库表数据
//////////////////////////////////////////////////////////
// 【函数】handleGetTableData —— 分页查看表结构 + 表数据
//
// 【前因后果 / 链路位置】
//   数据表页的主体接口：一次返回表结构（列名+类型）和当前页数据，前端一次
//   渲染整表。★ 沙箱双读设计的读取侧：默认读临时表副本（修改类 SQL 的效果
//   在这里可见），forceOriginal=true 读原表（用户想看未修改的原始数据时）——
//   两个开关配合 handleGetConnectionStatus 的临时表列表使用。
//   响应形状（columnInfo/tableData）是前端表格组件的强契约。
//////////////////////////////////////////////////////////
void GatewayServiceImpl::handleGetTableData(const httplib::Request& req, httplib::Response& res) {
    INF("Handling get table data request");
    // step1: 解析 JSON body（分页参数带默认值：第 1 页、每页 50 行）
    auto jsonOpt = biteutil::JSON::unserialize(req.body);
    if (!jsonOpt) {
        ERR("Failed to parse request body");
        sendErrorResponse(res, "", 400, "Invalid request body");
        return;
    }
    // step2: 提取参数（forceOriginal：true 读原表，默认读沙箱副本）
    Json::Value requestJson = jsonOpt.value();
    std::string requestId = requestJson.get("requestId", "").asString();
    std::string sessionId = requestJson.get("sessionId", "").asString();
    std::string dbConnectId = requestJson.get("dbConnectId", "").asString();
    std::string tableName = requestJson.get("tableName", "").asString();
    bool forceOriginal = requestJson.get("forceOriginal", false).asBool();
    int pageNumber = requestJson.get("pageNumber", 1).asInt();
    int pageSize = requestJson.get("pageSize", 50).asInt();
    // step3: 鉴权
    std::string userId;
    if (!validateSession(requestId, sessionId, res, userId)) {
        return;
    }
    // step4: 取信道 → 5. 构建请求（含分页与沙箱开关，全量下传）
    auto dbChannel = _svcChannels->getNode(FLAGS_db_service);
    if (!dbChannel) {
        ERR("Failed to get DatabaseService channel");
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    chat2Data::DatabaseService::GetTableDataRequest request;
    request.set_request_id(requestId);
    request.set_session_id(sessionId);
    request.set_db_connect_id(dbConnectId);
    request.set_table_name(tableName);
    request.set_force_original(forceOriginal);
    request.set_page_number(pageNumber);
    request.set_page_size(pageSize);
    // step6: 调用 + 检测（模板）
    chat2Data::DatabaseService::GetTableDataResponse response;
    brpc::Controller controller;
    chat2Data::DatabaseService::DatabaseService_Stub dbStub(dbChannel.get());
    dbStub.GetTableData(&controller, &request, &response, nullptr);
    if (controller.Failed()) {
        ERR("GetTableData RPC failed: {}", controller.ErrorText());
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    if (response.error_code() != 0) {
        sendErrorResponse(res, response.request_id(), response.error_code(),
                          response.error_msg());
        return;
    }
    // step7: 组装响应——tableSchema 两层：columnInfo（结构）+ tableData（分页数据）
    Json::Value responseJson;
    responseJson["requestId"] = response.request_id();
    responseJson["errorCode"] = response.error_code();
    responseJson["errorMsg"] = response.error_msg();
    if (response.has_result()) {
        Json::Value resultJson;
        if (response.result().has_table_schema()) {
            Json::Value schemaJson;
            const auto& schema = response.result().table_schema();
            Json::Value columnInfoJson;
            for (int i = 0; i < schema.column_info_size(); ++i) {
                Json::Value colJson;
                colJson["name"] = schema.column_info(i).name();
                colJson["type"] = schema.column_info(i).type();
                columnInfoJson.append(colJson);
            }
            schemaJson["columnInfo"] = columnInfoJson;
            if (schema.has_table_data()) {
                Json::Value tableDataJson;
                tableDataJson["totalRows"] = schema.table_data().total_rows();
                tableDataJson["currentPage"] = schema.table_data().current_page();
                tableDataJson["totalPages"] = schema.table_data().total_pages();
                tableDataJson["pageSize"] = schema.table_data().page_size();
                Json::Value rowsJson;
                for (int i = 0; i < schema.table_data().rows_size(); ++i) {
                    Json::Value rowJson;
                    for (int j = 0; j < schema.table_data().rows(i).cells_size(); ++j) {
                        rowJson.append(schema.table_data().rows(i).cells(j));
                    }
                    rowsJson.append(rowJson);
                }
                tableDataJson["rows"] = rowsJson;
                schemaJson["tableData"] = tableDataJson;
            }
            resultJson["tableSchema"] = schemaJson;
        }
        responseJson["result"] = resultJson;
    }
    auto jsonStr = biteutil::JSON::serialize(responseJson);
    if (jsonStr) {
        sendJsonResponse(res, jsonStr.value());
    } else {
        sendErrorResponse(res, requestId, 500, "Internal server error");
    }
    INF("Get table data request handled successfully, requestId: {}", requestId);
}

/* 【使用示例】
 *   curl -X POST 'http://dev-env-service:9000/api/db/table/data' \
 *        -H 'Content-Type: application/json' \
 *        -d '{"requestId":"req-d4","sessionId":"<sessionId>","dbConnectId":"conn_xxx",
 *             "tableName":"Sheet1_xxx","forceOriginal":false,"pageNumber":1,"pageSize":50}'
 *   响应 result 形态：
 *   {"tableSchema":{"columnInfo":[{"name":"部门","type":"VARCHAR"}],
 *    "tableData":{"totalRows":5,"currentPage":1,"totalPages":1,"pageSize":50,
 *                 "rows":[["行政部","曹玉凤","3350"],...]}}}
 */

// 获取数据库连接状态
//////////////////////////////////////////////////////////
// 【函数】handleGetConnectionStatus —— 查询连接下的临时表列表
//
// 【前因后果 / 链路位置】
//   17 章"修改类 SQL 沙箱"的读取侧：修改类 SQL 在临时表（xxx_temp_<毫秒>）
//   上执行，本接口让前端在数据表页展示"该连接当前有哪些临时表/是否在沙箱态"，
//   用户可选择 forceOriginal 看原表（配合 handleGetTableData 的 forceOriginal 参数）。
//   ★ GW1 修复：课件此接口无 sessionId 参数、无 validateSession（29 个接口里
//   唯一的漏网）——临时表名可推断业务数据结构，属需鉴权信息，且全站一致性要求
//   所有接口都验会话。修复版已补 sessionId + validateSession（课件原版见 20 章
//   课件 4.5 节）。
//////////////////////////////////////////////////////////
void GatewayServiceImpl::handleGetConnectionStatus(const httplib::Request& req, httplib::Response& res) {
    INF("Handling get connection status request");
    // step1: 解析 JSON body（★ GW1 修复点：补 sessionId 参数）
    auto jsonOpt = biteutil::JSON::unserialize(req.body);
    if (!jsonOpt) {
        ERR("Failed to parse request body");
        sendErrorResponse(res, "", 400, "Invalid request body");
        return;
    }
    Json::Value requestJson = jsonOpt.value();
    std::string requestId = requestJson.get("requestId", "").asString();
    std::string sessionId = requestJson.get("sessionId", "").asString();
    std::string dbConnectId = requestJson.get("dbConnectId", "").asString();
    // step2: 鉴权（★ 课件原版无此步——GW1 修复，与全站 28 个接口对齐）
    std::string userId;
    if (!validateSession(requestId, sessionId, res, userId)) {
        return;
    }
    // step3: 取信道 → 4. 构建 GetConnTempTablesRequest（temp_tables/has_temp_tables
    //        为响应顶层字段——GW4 已核对 17 章 dbService.proto 216-217 行，无缺陷）
    auto dbChannel = _svcChannels->getNode(FLAGS_db_service);
    if (!dbChannel) {
        ERR("Failed to get DatabaseService channel");
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    chat2Data::DatabaseService::GetConnTempTablesRequest request;
    request.set_request_id(requestId);
    request.set_db_connect_id(dbConnectId);
    // step5: 调用 + 检测（模板）
    chat2Data::DatabaseService::GetConnTempTablesResponse response;
    brpc::Controller controller;
    chat2Data::DatabaseService::DatabaseService_Stub dbStub(dbChannel.get());
    dbStub.GetConnTempTables(&controller, &request, &response, nullptr);
    if (controller.Failed()) {
        ERR("GetConnTempTables RPC failed: {}", controller.ErrorText());
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    if (response.error_code() != 0) {
        sendErrorResponse(res, response.request_id(), response.error_code(),
                          response.error_msg());
        return;
    }
    // step6: 组响应（顶层字段直取，无 result 包裹）
    Json::Value responseJson;
    responseJson["requestId"] = response.request_id();
    responseJson["errorCode"] = response.error_code();
    responseJson["errorMsg"] = response.error_msg();
    Json::Value resultJson;
    Json::Value tempTablesJson;
    for (int i = 0; i < response.temp_tables_size(); ++i) {
        tempTablesJson.append(response.temp_tables(i));
    }
    resultJson["tempTables"] = tempTablesJson;
    resultJson["hasTempTables"] = response.has_temp_tables();
    responseJson["result"] = resultJson;
    auto jsonStr = biteutil::JSON::serialize(responseJson);
    if (jsonStr) {
        sendJsonResponse(res, jsonStr.value());
    } else {
        sendErrorResponse(res, requestId, 500, "Internal server error");
    }
    INF("Get connection status request handled successfully, requestId: {}", requestId);
}

/* 【使用示例】
 *   curl -X POST 'http://dev-env-service:9000/api/db/connection/status' \
 *        -H 'Content-Type: application/json' \
 *        -d '{"requestId":"req-d5","sessionId":"<sessionId>","dbConnectId":"conn_xxx"}'
 *   响应：{"requestId":"req-d5","errorCode":0,...,
 *          "result":{"tempTables":["Sheet1_x_temp_1789533000"],"hasTempTables":true}}
 */

////////////////////////////////// AI子服务接口 //////////////////////////////////
//////////////////////////////////////////////////////////
// 【函数】handleGetModels —— 获取支持的模型列表
//
// 【前因后果 / 链路位置】
//   前端 AI 助手"新建会话"弹窗里模型下拉框的数据源。网关鉴权后 RPC 调
//   AI 子服务的 GetModels（18 章）——AI 侧从 ChatSDK 配置（.env 模型清单
//   与描述）读取支持的模型集合原样返回。链路：
//   前端 → 网关 → AI.GetModels → ChatSDK 配置。
//   响应形态：result.modelList[{modelName, modelDesc}]。
//////////////////////////////////////////////////////////
void GatewayServiceImpl::handleGetModels(const httplib::Request& req, httplib::Response& res) {
    INF("Handling get models request");
    // step1: 解析 JSON body
    auto jsonOpt = biteutil::JSON::unserialize(req.body);
    if (!jsonOpt) {
        ERR("Failed to parse request body");
        sendErrorResponse(res, "", 400, "Invalid request body");
        return;
    }
    // step2: 提取参数（本接口无需业务参数）
    Json::Value requestJson = jsonOpt.value();
    std::string requestId = requestJson.get("requestId", "").asString();
    std::string sessionId = requestJson.get("sessionId", "").asString();
    // step3: 鉴权
    std::string userId;
    if (!validateSession(requestId, sessionId, res, userId)) {
        return;
    }
    // step4: 获取 AIService 信道
    auto aiChannel = _svcChannels->getNode(FLAGS_ai_service);
    if (!aiChannel) {
        ERR("Failed to get AIService channel");
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    // step5: 构建 RPC 请求（仅 request_id）
    chat2Data::AiService::GetModelsRequest request;
    request.set_request_id(requestId);
    // step6: 发起 RPC 调用
    chat2Data::AiService::GetModelsResponse response;
    brpc::Controller controller;
    chat2Data::AiService::AIService_Stub stub(aiChannel.get());
    stub.GetModels(&controller, &request, &response, nullptr);
    // step7: 检测 RPC 调用
    if (controller.Failed()) {
        ERR("GetModels RPC failed: {}", controller.ErrorText());
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    if (response.error_code() != 0) {
        sendErrorResponse(res, response.request_id(), response.error_code(),
                          response.error_msg());
        return;
    }
    // step8: 组装响应（models[] → modelList[{modelName, modelDesc}]）
    Json::Value responseJson;
    responseJson["requestId"] = response.request_id();
    responseJson["errorCode"] = response.error_code();
    responseJson["errorMsg"] = response.error_msg();
    if (response.has_result()) {
        Json::Value resultJson;
        Json::Value modelListJson;
        for (int i = 0; i < response.result().models_size(); ++i) {
            Json::Value modelJson;
            modelJson["modelName"] = response.result().models(i).name();
            modelJson["modelDesc"] = response.result().models(i).desc();
            modelListJson.append(modelJson);
        }
        resultJson["modelList"] = modelListJson;
        responseJson["result"] = resultJson;
    }
    auto jsonStr = biteutil::JSON::serialize(responseJson);
    if (jsonStr) {
        sendJsonResponse(res, jsonStr.value());
    } else {
        sendErrorResponse(res, requestId, 500, "Internal server error");
    }
    INF("Get models request handled successfully, requestId: {}", requestId);
}

/* 【使用示例】
 *   curl -X POST 'http://dev-env-service:9000/api/ai/models' \
 *        -H 'Content-Type: application/json' \
 *        -d '{"requestId":"req-a1","sessionId":"<sessionId>"}'
 *   响应 result 形态：
 *   {"modelList":[{"modelName":"glm-4-flash","modelDesc":"GLM-4-Flash 快速回复"}]}
 */

//////////////////////////////////////////////////////////
// 【函数】handleCreateChatSession —— 新建聊天会话
//
// 【前因后果 / 链路位置】
//   用户在 AI 助手页选好模型与场景后点"新建会话"。网关鉴权（拿到
//   userId）后 RPC 调 AI.CreateSession——AI 侧（18 章）向 MySQL
//   tbl_chatSession 落一行并向内部 SQLite 消息库建会话，返回
//   chatSessionId；此后该会话所有 SendMessage 均携带此 ID 做上下文。
//   链路：前端 → 网关 → AI.CreateSession → tbl_chatSession + SQLite。
//   字段注意：proto 用 model（非 model_name）；dbConnectionInfo 仅
//   database 场景携带（连接信息 JSON，AI 侧存库供后续查询上下文）。
//   响应形态：result.{chatSessionId, modelName}。
//////////////////////////////////////////////////////////
void GatewayServiceImpl::handleCreateChatSession(const httplib::Request& req, httplib::Response& res) {
    INF("Handling create chat session request");
    // step1: 解析 JSON body
    auto jsonOpt = biteutil::JSON::unserialize(req.body);
    if (!jsonOpt) {
        ERR("Failed to parse request body");
        sendErrorResponse(res, "", 400, "Invalid request body");
        return;
    }
    // step2: 提取参数
    Json::Value requestJson = jsonOpt.value();
    std::string requestId = requestJson.get("requestId", "").asString();
    std::string sessionId = requestJson.get("sessionId", "").asString();
    std::string modelName = requestJson.get("modelName", "").asString();
    std::string sessionType = requestJson.get("sessionType", "").asString();       // excel/database/plain
    std::string dbConnectionInfo = requestJson.get("dbConnectionInfo", "").asString();
    // step3: 鉴权（userId 由会话反查——AI 侧按 user_id 落库，前端伪造不了归属）
    std::string userId;
    if (!validateSession(requestId, sessionId, res, userId)) {
        return;
    }
    // step4: 获取 AIService 信道
    auto aiChannel = _svcChannels->getNode(FLAGS_ai_service);
    if (!aiChannel) {
        ERR("Failed to get AIService channel");
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    // step5: 构建 RPC 请求
    chat2Data::AiService::CreateChatSessionRequest request;
    request.set_request_id(requestId);
    request.set_user_id(userId);
    request.set_model(modelName);
    request.set_session_type(sessionType);
    request.set_db_connection_info(dbConnectionInfo);
    // step6: 发起 RPC 调用
    chat2Data::AiService::CreateChatSessionResponse response;
    brpc::Controller controller;
    chat2Data::AiService::AIService_Stub stub(aiChannel.get());
    stub.CreateSession(&controller, &request, &response, nullptr);
    // step7: 检测 RPC 调用
    if (controller.Failed()) {
        ERR("CreateSession RPC failed: {}", controller.ErrorText());
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    if (response.error_code() != 0) {
        sendErrorResponse(res, response.request_id(), response.error_code(),
                          response.error_msg());
        return;
    }
    // step8: 组装响应（回传 chatSessionId——后续 SendMessage/History/Delete 的句柄）
    Json::Value responseJson;
    responseJson["requestId"] = response.request_id();
    responseJson["errorCode"] = response.error_code();
    responseJson["errorMsg"] = response.error_msg();
    if (response.has_result()) {
        Json::Value resultJson;
        resultJson["chatSessionId"] = response.result().session().chat_session_id();
        resultJson["modelName"] = response.result().session().model();
        responseJson["result"] = resultJson;
    }
    auto jsonStr = biteutil::JSON::serialize(responseJson);
    if (jsonStr) {
        sendJsonResponse(res, jsonStr.value());
    } else {
        sendErrorResponse(res, requestId, 500, "Internal server error");
    }
    INF("Create chat session request handled successfully, requestId: {}", requestId);
}

/* 【使用示例】
 *   curl -X POST 'http://dev-env-service:9000/api/ai/session/create' \
 *        -H 'Content-Type: application/json' \
 *        -d '{"requestId":"req-a2","sessionId":"<sessionId>",
 *             "modelName":"glm-4-flash","sessionType":"plain"}'
 *   响应 result 形态：
 *   {"chatSessionId":"cs_xxxx-xxxxxxxx-yyyy","modelName":"glm-4-flash"}
 */

//////////////////////////////////////////////////////////
// 【函数】handleGetChatSessionLists —— 获取聊天会话列表
//
// 【前因后果 / 链路位置】
//   前端 AI 助手侧边栏"历史会话"列表的数据源。网关鉴权后 RPC 调
//   AI.GetSessions——AI 侧按 user_id 查 tbl_chatSession（归属隔离，
//   只能看到自己的会话）。列表项里的 title 由 AI 侧四阶段输出协议
//   TITLE 标签生成；无标题时回退 first_user_message_content 前 20 字。
//   链路：前端 → 网关 → AI.GetSessions → tbl_chatSession（按 user_id）。
//   响应形态：result.chatSessionLists[{chatSessionId, modelName, title,
//   createdAt, updatedAt, messageCount, firstUserMessageContent}]。
//////////////////////////////////////////////////////////
void GatewayServiceImpl::handleGetChatSessionLists(const httplib::Request& req, httplib::Response& res) {
    INF("Handling get chat session lists request");
    // step1: 解析 JSON body
    auto jsonOpt = biteutil::JSON::unserialize(req.body);
    if (!jsonOpt) {
        ERR("Failed to parse request body");
        sendErrorResponse(res, "", 400, "Invalid request body");
        return;
    }
    // step2: 提取参数
    Json::Value requestJson = jsonOpt.value();
    std::string requestId = requestJson.get("requestId", "").asString();
    std::string sessionId = requestJson.get("sessionId", "").asString();
    // step3: 鉴权（userId 即列表的过滤键——归属隔离在 AI 侧完成）
    std::string userId;
    if (!validateSession(requestId, sessionId, res, userId)) {
        return;
    }
    // step4: 获取 AIService 信道
    auto aiChannel = _svcChannels->getNode(FLAGS_ai_service);
    if (!aiChannel) {
        ERR("Failed to get AIService channel");
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    // step5: 构建 RPC 请求
    chat2Data::AiService::GetSessionsRequest request;
    request.set_request_id(requestId);
    request.set_user_id(userId);
    // step6: 发起 RPC 调用
    chat2Data::AiService::GetSessionsResponse response;
    brpc::Controller controller;
    chat2Data::AiService::AIService_Stub stub(aiChannel.get());
    stub.GetSessions(&controller, &request, &response, nullptr);
    // step7: 检测 RPC 调用
    if (controller.Failed()) {
        ERR("GetSessions RPC failed: {}", controller.ErrorText());
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    if (response.error_code() != 0) {
        sendErrorResponse(res, response.request_id(), response.error_code(),
                          response.error_msg());
        return;
    }
    // step8: 组装响应（sessioninfo[] → chatSessionLists[]；时间戳 int64 直传秒级值）
    Json::Value responseJson;
    responseJson["requestId"] = response.request_id();
    responseJson["errorCode"] = response.error_code();
    responseJson["errorMsg"] = response.error_msg();
    if (response.has_result()) {
        Json::Value resultJson;
        Json::Value chatSessionListsJson;
        for (int i = 0; i < response.result().sessioninfo_size(); ++i) {
            Json::Value sessionJson;
            sessionJson["chatSessionId"] = response.result().sessioninfo(i).id();
            sessionJson["modelName"] = response.result().sessioninfo(i).model();
            sessionJson["title"] = response.result().sessioninfo(i).title();
            sessionJson["createdAt"] = static_cast<Json::Int64>(
                response.result().sessioninfo(i).created_at());
            sessionJson["updatedAt"] = static_cast<Json::Int64>(
                response.result().sessioninfo(i).updated_at());
            sessionJson["messageCount"] = response.result().sessioninfo(i).message_count();
            sessionJson["firstUserMessageContent"] =
                response.result().sessioninfo(i).first_user_message_content();
            chatSessionListsJson.append(sessionJson);
        }
        resultJson["chatSessionLists"] = chatSessionListsJson;
        responseJson["result"] = resultJson;
    }
    auto jsonStr = biteutil::JSON::serialize(responseJson);
    if (jsonStr) {
        sendJsonResponse(res, jsonStr.value());
    } else {
        sendErrorResponse(res, requestId, 500, "Internal server error");
    }
    INF("Get chat session lists request handled successfully, requestId: {}", requestId);
}

/* 【使用示例】
 *   curl -X POST 'http://dev-env-service:9000/api/ai/chatSessionLists' \
 *        -H 'Content-Type: application/json' \
 *        -d '{"requestId":"req-a3","sessionId":"<sessionId>"}'
 *   响应 result 形态：
 *   {"chatSessionLists":[{"chatSessionId":"cs_xxx","modelName":"glm-4-flash",
 *    "title":"帮我分析销售额","createdAt":1789533000,"updatedAt":1789533600,
 *    "messageCount":6,"firstUserMessageContent":"帮我分析销售额趋势"}]}
 */

//////////////////////////////////////////////////////////
// 【函数】handleGetHistory —— 获取聊天会话历史消息
//
// 【前因后果 / 链路位置】
//   前端点击侧边栏某会话项时回显完整聊天记录。网关鉴权后 RPC 调
//   AI.GetSessionHistory——AI 侧按 user_id 校验会话归属（防越权读
//   他人会话）后查内部 SQLite 消息表（ChatSDK 内部库，业务侧
//   tbl_chatSession 只存会话元数据，消息体在 SQLite）。
//   result 的 fileId/sessionType/dbConnectionInfo 是前端路由提示：
//   Excel 场景跳 Excel 页用 fileId 预览文件；database 场景跳数据库页
//   用 dbConnectionInfo 恢复连接——proto3 省略默认值字段，空即未设置，
//   所以下发前逐字段判空（课件原样）。
//   链路：前端 → 网关 → AI.GetSessionHistory → 归属校验 → SQLite 消息表。
//   响应形态：result.{fileId?, sessionType?, dbConnectionInfo?,
//   messageList[{id, role, content, timestamp}]}。
//////////////////////////////////////////////////////////
void GatewayServiceImpl::handleGetHistory(const httplib::Request& req, httplib::Response& res) {
    INF("Handling get history request");
    // step1: 解析 JSON body
    auto jsonOpt = biteutil::JSON::unserialize(req.body);
    if (!jsonOpt) {
        ERR("Failed to parse request body");
        sendErrorResponse(res, "", 400, "Invalid request body");
        return;
    }
    // step2: 提取参数
    Json::Value requestJson = jsonOpt.value();
    std::string requestId = requestJson.get("requestId", "").asString();
    std::string sessionId = requestJson.get("sessionId", "").asString();
    std::string chatSessionId = requestJson.get("chatSessionId", "").asString();
    // step3: 鉴权（userId 供 AI 侧做会话归属校验）
    std::string userId;
    if (!validateSession(requestId, sessionId, res, userId)) {
        return;
    }
    // step4: 获取 AIService 信道
    auto aiChannel = _svcChannels->getNode(FLAGS_ai_service);
    if (!aiChannel) {
        ERR("Failed to get AIService channel");
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    // step5: 构建 RPC 请求
    chat2Data::AiService::GetSessionHistoryRequest request;
    request.set_request_id(requestId);
    request.set_user_id(userId);
    request.set_chat_session_id(chatSessionId);
    // step6: 发起 RPC 调用
    chat2Data::AiService::GetSessionHistoryResponse response;
    brpc::Controller controller;
    chat2Data::AiService::AIService_Stub stub(aiChannel.get());
    stub.GetSessionHistory(&controller, &request, &response, nullptr);
    // step7: 检测 RPC 调用
    if (controller.Failed()) {
        ERR("GetSessionHistory RPC failed: {}", controller.ErrorText());
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    if (response.error_code() != 0) {
        sendErrorResponse(res, response.request_id(), response.error_code(),
                          response.error_msg());
        return;
    }
    // step8: 组装响应（路由提示字段逐个判空下发 + messages[] → messageList[]）
    Json::Value responseJson;
    responseJson["requestId"] = response.request_id();
    responseJson["errorCode"] = response.error_code();
    responseJson["errorMsg"] = response.error_msg();
    if (response.has_result()) {
        Json::Value resultJson;
        if (!response.result().file_id().empty()) {
            resultJson["fileId"] = response.result().file_id();
        }
        if (!response.result().session_type().empty()) {
            resultJson["sessionType"] = response.result().session_type();
        }
        if (!response.result().db_connection_info().empty()) {
            resultJson["dbConnectionInfo"] = response.result().db_connection_info();
        }
        Json::Value messageListJson;
        for (int i = 0; i < response.result().messages_size(); ++i) {
            Json::Value msgJson;
            msgJson["id"] = response.result().messages(i).id();
            msgJson["role"] = response.result().messages(i).role();
            msgJson["content"] = response.result().messages(i).content();
            msgJson["timestamp"] = static_cast<Json::Int64>(
                response.result().messages(i).timestamp());
            messageListJson.append(msgJson);
        }
        resultJson["messageList"] = messageListJson;
        responseJson["result"] = resultJson;
    }
    auto jsonStr = biteutil::JSON::serialize(responseJson);
    if (jsonStr) {
        sendJsonResponse(res, jsonStr.value());
    } else {
        sendErrorResponse(res, requestId, 500, "Internal server error");
    }
    INF("Get history request handled successfully, requestId: {}", requestId);
}

/* 【使用示例】
 *   curl -X POST 'http://dev-env-service:9000/api/ai/history' \
 *        -H 'Content-Type: application/json' \
 *        -d '{"requestId":"req-a4","sessionId":"<sessionId>",
 *             "chatSessionId":"cs_xxxx-xxxxxxxx-yyyy"}'
 *   响应 result 形态（plain 场景无 fileId/sessionType）：
 *   {"messageList":[{"id":"msg_xxx","role":"user","content":"你好",
 *    "timestamp":1789533000},{"id":"msg_yyy","role":"assistant",
 *    "content":"你好！有什么可以帮你？","timestamp":1789533001}]}
 */

//////////////////////////////////////////////////////////
// 【函数】handleDeleteChatSession —— 删除聊天会话
//
// 【前因后果 / 链路位置】
//   前端侧边栏会话项的删除按钮。网关鉴权后 RPC 调 AI.DeleteSession——
//   AI 侧（18 章）先删内部 SQLite 消息表记录，再删 MySQL tbl_chatSession
//   行（归属校验不过则拒删）。tbl_chatSession.fileId 对 tbl_fileInfo 的
//   外键是"会话引用文件"方向，删会话只删引用行本身，不会级联删文件。
//   链路：前端 → 网关 → AI.DeleteSession → SQLite 消息 + tbl_chatSession。
//   响应形态：无 result，errorCode=0 即删除成功。
//////////////////////////////////////////////////////////
void GatewayServiceImpl::handleDeleteChatSession(const httplib::Request& req, httplib::Response& res) {
    INF("Handling delete chat session request");
    // step1: 解析 JSON body
    auto jsonOpt = biteutil::JSON::unserialize(req.body);
    if (!jsonOpt) {
        ERR("Failed to parse request body");
        sendErrorResponse(res, "", 400, "Invalid request body");
        return;
    }
    // step2: 提取参数
    Json::Value requestJson = jsonOpt.value();
    std::string requestId = requestJson.get("requestId", "").asString();
    std::string sessionId = requestJson.get("sessionId", "").asString();
    std::string chatSessionId = requestJson.get("chatSessionId", "").asString();
    // step3: 鉴权（userId 供 AI 侧做删除归属校验）
    std::string userId;
    if (!validateSession(requestId, sessionId, res, userId)) {
        return;
    }
    // step4: 获取 AIService 信道
    auto aiChannel = _svcChannels->getNode(FLAGS_ai_service);
    if (!aiChannel) {
        ERR("Failed to get AIService channel");
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    // step5: 构建 RPC 请求
    chat2Data::AiService::DeleteSessionRequest request;
    request.set_request_id(requestId);
    request.set_user_id(userId);
    request.set_chat_session_id(chatSessionId);
    // step6: 发起 RPC 调用
    chat2Data::AiService::DeleteSessionResponse response;
    brpc::Controller controller;
    chat2Data::AiService::AIService_Stub stub(aiChannel.get());
    stub.DeleteSession(&controller, &request, &response, nullptr);
    // step7: 检测 RPC 调用
    if (controller.Failed()) {
        ERR("DeleteSession RPC failed: {}", controller.ErrorText());
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    if (response.error_code() != 0) {
        sendErrorResponse(res, response.request_id(), response.error_code(),
                          response.error_msg());
        return;
    }
    // step8: 组装响应（无 result）
    Json::Value responseJson;
    responseJson["requestId"] = response.request_id();
    responseJson["errorCode"] = response.error_code();
    responseJson["errorMsg"] = response.error_msg();
    auto jsonStr = biteutil::JSON::serialize(responseJson);
    if (jsonStr) {
        sendJsonResponse(res, jsonStr.value());
    } else {
        sendErrorResponse(res, requestId, 500, "Internal server error");
    }
    INF("Delete chat session request handled successfully, requestId: {}", requestId);
}

/* 【使用示例】
 *   curl -X POST 'http://dev-env-service:9000/api/ai/delete' \
 *        -H 'Content-Type: application/json' \
 *        -d '{"requestId":"req-a5","sessionId":"<sessionId>",
 *             "chatSessionId":"cs_xxxx-xxxxxxxx-yyyy"}'
 *   响应：{"requestId":"req-a5","errorCode":0,"errorMsg":"success"}
 */

//////////////////////////////////////////////////////////
// 【函数】handleAiChat —— 发送聊天消息（SSE 流式透传，29 接口里唯一的非 Stub 接口）
//
// 【前因后果 / 链路位置】
//   前端聊天框发消息后要"边生成边看"的打字机效果。AI 侧（18 章）
//   SendMessage 是服务端流式 RPC（stream SendMessageResponse），brpc
//   Stub 的一次 Request/Response 建模拿不到逐块推送——AI 侧把该 RPC
//   同时暴露为 HTTP+SSE（brpc::ProgressiveAttachment 逐块推
//   "data: xxx\n\n"，收尾 "data: [DONE]\n\n"）。因此网关不走 brpc 信道，
//   而是用 httplib::Client 手动转发 HTTP，并经 set_chunked_content_provider
//   把 AI 返回的 SSE 块原样透传给前端。
//   完整链路：前端 → 网关（chunked provider）→ httplib::Client POST
//   /chat2Data.AiService.AIService/SendMessage（snake_case JSON body，
//   AI 侧 handleSendMessage 直接反序列化 JSON 构建 pb）→ AI 17 步消息
//   主流程（归属校验/落消息/GLM 流式生成/回复落库）→ content_receiver
//   逐块原样透传 → 网关兜底 [DONE]。
//   ★ 取 getNodeAddr 而非 getNode：透传要裸 host:port 建 HTTP 客户端，
//   不走 pb 序列化栈（brpc 信道只服务普通接口）。
//   ★ GW2（已裁决保留）：AI 侧 writeChunk(done=true) 已发一次
//   "data: [DONE]\n\n"，本接口 step7.9 再补一次 → 正常路径前端收到
//   两个 DONE。保留原因：SSE 解析器按事件处理，重复 DONE 无害；而
//   AI 侧异常中断（进程崩溃/超时）时未发 DONE，网关兜底保证前端流
//   不悬挂——这是课件刻意的防御设计。
//////////////////////////////////////////////////////////
void GatewayServiceImpl::handleAiChat(const httplib::Request& req, httplib::Response& res) {
    INF("Handling ai chat request");
    // step1: 解析 JSON body
    auto jsonOpt = biteutil::JSON::unserialize(req.body);
    if (!jsonOpt) {
        ERR("Failed to parse request body");
        sendErrorResponse(res, "", 400, "Invalid request body");
        return;
    }
    // step2: 提取参数（chatType: excel/database/plain；dbType 为 DataType
    //        枚举值 0/1/2，网关只透传 int，映射由 AI 侧完成）
    Json::Value requestJson = jsonOpt.value();
    std::string requestId = requestJson.get("requestId", "").asString();
    std::string sessionId = requestJson.get("sessionId", "").asString();
    std::string chatSessionId = requestJson.get("chatSessionId", "").asString();
    std::string message = requestJson.get("message", "").asString();
    std::string chatType = requestJson.get("chatType", "").asString();
    std::string fileId = requestJson.get("fileId", "").asString();
    int dbType = requestJson.get("dbType", 0).asInt();
    std::string dbConnectId = requestJson.get("dbConnectId", "").asString();
    std::string tableName = requestJson.get("tableName", "").asString();
    // step3: 鉴权
    std::string userId;
    if (!validateSession(requestId, sessionId, res, userId)) {
        return;
    }
    // step4: 获取 AI 子服务的服务器地址（host:port——建 HTTP 客户端用）
    auto aiAddrOpt = _svcChannels->getNodeAddr(FLAGS_ai_service);
    if (!aiAddrOpt) {
        ERR("Failed to get AIService address");
        sendErrorResponse(res, requestId, 503, "Service not available");
        return;
    }
    std::string aiAddr = aiAddrOpt.value();
    INF("AIService address: {}", aiAddr);
    // step5: 构建转发请求体（★ snake_case：AI 侧 HTTP handler 直接解析
    //        JSON 构 pb，字段名与 proto 对齐，与普通接口的 camelCase 命名不同）
    Json::Value aiRequestJson;
    aiRequestJson["request_id"] = requestId;
    aiRequestJson["session_id"] = sessionId;
    aiRequestJson["user_id"] = userId;
    aiRequestJson["chat_session_id"] = chatSessionId;
    aiRequestJson["chat_type"] = chatType;
    aiRequestJson["message"] = message;
    aiRequestJson["file_id"] = fileId;
    aiRequestJson["db_type"] = dbType;
    aiRequestJson["db_connect_id"] = dbConnectId;
    aiRequestJson["table_name"] = tableName;
    auto requestBodyOpt = biteutil::JSON::serialize(aiRequestJson);
    if (!requestBodyOpt) {
        ERR("Failed to serialize request body");
        sendErrorResponse(res, requestId, 500, "Internal server error");
        return;
    }
    std::string requestBody = requestBodyOpt.value();
    // step6: 设置 SSE 响应头并挂载分块回调——此调用立即返回，实际转发
    //        在回调里异步进行（httplib 在响应期间反复调 provider 取数据）
    res.status = 200;
    res.set_header("Cache-Control", "no-cache");
    res.set_header("Connection", "keep-alive");
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Headers", "*");
    // step7: 分块回调——向 AI 子服务发起 HTTP 请求并透传 SSE 流
    res.set_chunked_content_provider("text/event-stream",
        [this, aiAddr, requestBody](size_t offset, httplib::DataSink& dataSink) -> bool {
        // step7.1: 解析 AI 服务地址 "host:port"
        size_t colonPos = aiAddr.find(':');
        if (colonPos == std::string::npos) {
            ERR("Invalid AIService address format: {}", aiAddr);
            std::string errorData = "data: [ERROR]\n\n";
            dataSink.write(errorData.c_str(), errorData.size());
            dataSink.done();
            return false;
        }
        std::string host = aiAddr.substr(0, colonPos);
        int port = std::stoi(aiAddr.substr(colonPos + 1));
        // step7.2: 创建 HTTP 客户端（每次回调新建短连接——流式一次性场景，
        //          不复用长连接，避免 provider 与连接池生命周期纠缠）
        httplib::Client client(host, port);
        // step7.3: 设置超时——读/写 300s 覆盖 GLM 长回答生成耗时，
        //          连接 60s 只覆盖建连握手
        client.set_read_timeout(300, 0);
        client.set_write_timeout(300, 0);
        client.set_connection_timeout(60, 0);
        // step7.4: 构建 HTTP 请求（路径 = brpc 泛化服务的 HTTP URL 格式）
        httplib::Request req;
        req.method = "POST";
        req.path = "/chat2Data.AiService.AIService/SendMessage";
        req.headers = {
            {"Content-Type", "application/json"},
            {"Accept", "text/event-stream"}
        };
        req.body = requestBody;
        // step7.5: 响应头回调——非 200 即判定建连失败，终止请求
        bool connectionFailed = false;
        req.response_handler = [&](const httplib::Response& res) {
            if (res.status != 200) {
                connectionFailed = true;
                return false; // 终止请求
            }
            return true; // 继续接收后续响应数据(SSE)
        };
        // step7.6: 内容接收回调——AI 返回的 SSE 块原样写进响应流（不做
        //          任何格式加工，保持 "data: xxx\n\n" 逐块节奏）
        req.content_receiver = [&](const char* data, size_t len, size_t offset, size_t totalLength) {
            if (connectionFailed) {
                return false;
            }
            dataSink.write(data, len);
            return true;
        };
        // step7.7: 发送 HTTP 请求（阻塞直至 AI 侧流式响应结束或超时）
        auto httpResponse = client.send(req);
        // step7.8: 建连失败兜底——向前端推 [ERROR] 后收流
        if (!httpResponse) {
            ERR("HTTP request to AIService failed");
            std::string errorData = "data: [ERROR]\n\n";
            dataSink.write(errorData.c_str(), errorData.size());
            dataSink.done();
            return false;
        }
        // step7.9: 兜底结束标记（★ GW2：与 AI 侧的 [DONE] 重复，保留课件设计——
        //          正常路径重复 DONE 无害，异常路径防前端流悬挂）
        std::string doneData = "data: [DONE]\n\n";
        dataSink.write(doneData.c_str(), doneData.size());
        dataSink.done();
        return false;
    });
    INF("Ai chat request handled successfully, requestId: {}", requestId);
}

/* 【使用示例】
 *   curl -N -X POST 'http://dev-env-service:9000/api/ai/sendStreamMessage' \
 *        -H 'Content-Type: application/json' \
 *        -d '{"requestId":"req-a6","sessionId":"<sessionId>",
 *             "chatSessionId":"cs_xxxx-xxxxxxxx-yyyy","chatType":"plain",
 *             "message":"用一句话介绍你自己"}'
 *   响应（text/event-stream 逐块；★ G-H6 实测形态——AI 侧 writeChunk 直接
 *   推纯文本增量（非 pb2json JSON），经 content_receiver 原样透传到达前端）：
 *   data: 连接
 *   data: 成功
 *   data: [DONE]     ← AI 侧正常收尾（网关兜底再补一个，实测 [DONE] x2，前端按事件去重）
 */

} // end GatewayService

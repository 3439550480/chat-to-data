#include <signal.h>
#include <iostream>
#include <gflags/gflags.h>
#include <spdlog/spdlog.h>
#include <bite_scaffold/log.h>
#include <ai_chat_sdk/ChatSDK.h>
#include <ai_chat_sdk/util/myLog.h>
#include "aiServer.h"
#include "../common/envLoader.h"

// ETCD地址
DEFINE_string(etcd_addr, "http://dev-etcd:2379", "ETCD地址");
// AI子服务配置（AI6 修正：课件是 192.168.150.129 物理机地址，适配为容器网络）
DEFINE_int32(port, 9006, "AI服务RPC端口");
DEFINE_string(service_name, "AIService", "AI子服务名称");
DEFINE_string(service_addr, "dev-env-service:9006", "AI子服务地址");
// MySQL配置（tbl_chatSession 持久化）
DEFINE_string(mysql_host, "dev-mysql", "MySQL主机地址");
DEFINE_string(mysql_user, "root", "MySQL用户名");
DEFINE_string(mysql_passwd, "123456", "MySQL密码");
DEFINE_string(mysql_db, "chat2Data", "MySQL数据库名称");
DEFINE_int32(mysql_port, 3306, "MySQL端口号");
DEFINE_string(mysql_charset, "utf8mb4", "MySQL字符集");
DEFINE_int32(mysql_pool_size, 3, "MySQL连接池大小");
// Redis配置（会话元数据缓存）
DEFINE_string(redis_host, "dev-redis", "Redis主机地址");
DEFINE_int32(redis_port, 6379, "Redis端口号");
DEFINE_string(redis_passwd, "123456", "Redis密码");
DEFINE_int32(redis_db, 0, "Redis数据库索引");
DEFINE_int32(redis_pool_size, 3, "Redis连接池大小");
// ChatSDK 通用参数
DEFINE_double(temperature, 0.7, "温度值，影响生成文本的随机性");
DEFINE_int32(max_tokens, 2048, "最大token数");
// GLM 模型配置（真实值来自 .env，此处仅兜底默认；AI6：删除课件 Ollama 配置）
DEFINE_string(glm_model_name, "glm-4-flash", "GLM模型名称（.env 优先）");
DEFINE_string(glm_model_desc, "GLM 通用对话模型", "GLM模型描述（.env 优先）");
DEFINE_string(glm_base_url, "https://open.bigmodel.cn/api/paas/v4", "GLM接口base url（.env 优先）");
// 日志器配置
DEFINE_bool(log_async, false, "是否启用异步日志");
DEFINE_int32(log_level, 2, "日志输出等级: 1-debug;2-info;3-warn;4-error;6-off");
DEFINE_string(log_format, "[%H:%M:%S][%-7l]: %v", "日志输出格式");
DEFINE_string(log_path, "stdout", "日志输出路径");
// 跨服务调用 key（aiMessageHandler 的 DECLARE 配对）
DEFINE_string(file_service, "FileService", "文件子服务调用key");
DEFINE_string(db_service, "DatabaseService", "数据库子服务调用key");
DEFINE_string(user_service, "UserService", "用户子服务调用key");
DEFINE_string(notify_service, "NotifyService", "通知子服务调用key");
// 需要监听的服务名称（19 章四条依赖线：worksheet 表名/SQL 执行/用户邮箱/发邮件）
DEFINE_string(watch_file_service, "FileService", "watch:文件子服务");
DEFINE_string(watch_db_service, "DatabaseService", "watch:数据库子服务");
DEFINE_string(watch_user_service, "UserService", "watch:用户子服务");
DEFINE_string(watch_notify_service, "NotifyService", "watch:通知子服务");

void signalHandler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        exit(0);
    }
}

// 依次尝试候选路径加载 .env（服务从 build 目录启动 → app/.env 为 ../..）
static void loadAppEnv() {
    const char* candidates[] = {
        "../../.env", "../../../.env", "../.env", "../../../../.env"
    };
    for (const char* path : candidates) {
        if (chat2Data::loadEnvFile(path)) {
            return;
        }
    }
    WRN("no .env file found, fallback to process environment");
}

int main(int argc, char* argv[]) {
    // 1. 解析 gflags 参数
    google::ParseCommandLineFlags(&argc, &argv, true);
    // 2. 加载 .env（GLM_API_KEY/GLM_MODEL_NAME/GLM_BASE_URL/GLM_MODEL_DESC）
    loadAppEnv();
    // 3. 初始化日志器（★ 必须在 initModels 之前：SDK 内部日志依赖 bite::Logger）
    bitelog::log_settings logSettings;
    logSettings.async = FLAGS_log_async;
    logSettings.level = FLAGS_log_level;
    logSettings.format = FLAGS_log_format;
    logSettings.path = FLAGS_log_path;
    bitelog::bitelog_init(logSettings);
    bite::Logger::initLogger("aiService", FLAGS_log_path,
                             (spdlog::level::level_enum)FLAGS_log_level);
    INF("Loggers initialized (bitelog + sdk logger)");
    // 4. 注册信号处理函数
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    INF("Signal handlers registered");
    // 5. MySQL 配置（tbl_chatSession 持久化）
    aiService::MysqlConfig mysqlConfig;
    mysqlConfig._host = FLAGS_mysql_host;
    mysqlConfig._user = FLAGS_mysql_user;
    mysqlConfig._passwd = FLAGS_mysql_passwd;
    mysqlConfig._db = FLAGS_mysql_db;
    mysqlConfig._port = FLAGS_mysql_port;
    mysqlConfig._cset = FLAGS_mysql_charset;
    mysqlConfig._connectionPoolSize = FLAGS_mysql_pool_size;
    // 6. Redis 配置
    aiService::RedisConfig redisConfig;
    redisConfig._host = FLAGS_redis_host;
    redisConfig._port = FLAGS_redis_port;
    redisConfig._passwd = FLAGS_redis_passwd;
    redisConfig._db = FLAGS_redis_db;
    redisConfig._connectionPoolSize = FLAGS_redis_pool_size;
    // 7. 注册中心配置
    aiService::RegisterCenterConfig registerCenterConfig;
    registerCenterConfig._etcdAddr = FLAGS_etcd_addr;
    registerCenterConfig._serviceName = FLAGS_service_name;
    registerCenterConfig._serviceAddr = FLAGS_service_addr;
    // 8. ChatSDK 模型配置（AI6 修正：GLM 走 .env，删除课件 deepseek/gpt/gemini/Ollama）
    std::vector<std::shared_ptr<ai_chat_sdk::Config>> modelConfigs;
    auto glmConfig = std::make_shared<ai_chat_sdk::APIConfig>();
    glmConfig->_modelName = chat2Data::getEnvOrDefault("GLM_MODEL_NAME", FLAGS_glm_model_name);
    glmConfig->_apiKey = chat2Data::getEnvOrDefault("GLM_API_KEY");
    glmConfig->_baseUrl = chat2Data::getEnvOrDefault("GLM_BASE_URL", FLAGS_glm_base_url);
    glmConfig->_modelDesc = chat2Data::getEnvOrDefault("GLM_MODEL_DESC", FLAGS_glm_model_desc);
    glmConfig->_temperature = FLAGS_temperature;
    glmConfig->_maxTokens = FLAGS_max_tokens;
    if (glmConfig->_apiKey.empty()) {
        WRN("GLM_API_KEY 为空：模型注册后不可用（发消息会失败），仅会话管理等功能可测");
    }
    modelConfigs.push_back(glmConfig);
    // 9. 监听服务列表（四条依赖线全部 watch，与 ch19 的四个 getNode 对应）
    std::vector<std::string> watchServices;
    watchServices.push_back(FLAGS_watch_file_service);
    watchServices.push_back(FLAGS_watch_db_service);
    watchServices.push_back(FLAGS_watch_user_service);
    watchServices.push_back(FLAGS_watch_notify_service);
    // 10. 构建各实例
    auto rpcServer = aiService::AIServiceBuilder()
        .setMysqlConfig(mysqlConfig)
        .setRedisConfig(redisConfig)
        .setPort(FLAGS_port)
        .setEtcdAddr(FLAGS_etcd_addr)
        .setRegisterCenterConfig(registerCenterConfig)
        .setChatSDKConfig(modelConfigs)
        .setWatchServices(watchServices)
        .build();
    if (!rpcServer) {
        ERR("Failed to build AIServiceServer");
        return -1;
    }
    // 11. 启动 RPC 服务器（阻塞直至退出信号）
    INF("AIService is running on port {}", FLAGS_port);
    rpcServer->start();
    return 0;
}

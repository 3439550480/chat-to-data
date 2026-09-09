#include <signal.h>
#include <iostream>
#include <sstream>
#include <gflags/gflags.h>
#include <bite_scaffold/log.h>
#include "fileServer.h"
#include "common.h"

// ETCD地址
DEFINE_string(etcd_addr, "http://dev-etcd:2379", "ETCD地址");
// 文件子服务配置（课件问题㉖修复：192.168.150.129:9004 → 容器网络地址）
DEFINE_int32(port, 9004, "文件服务RPC端口");
DEFINE_string(service_name, "FileService", "文件子服务名称");
DEFINE_string(service_addr, "dev-env-service:9004", "文件子服务地址");
// MySQL配置（㉖：dev-mysql 容器网络）
DEFINE_string(mysql_host, "dev-mysql", "MySQL主机地址");
DEFINE_string(mysql_user, "root", "MySQL用户名");
DEFINE_string(mysql_passwd, "123456", "MySQL密码");
DEFINE_string(mysql_db, "chat2Data", "MySQL数据库名称");
DEFINE_int32(mysql_port, 3306, "MySQL端口号");
DEFINE_string(mysql_charset, "utf8mb4", "MySQL字符集");
// Redis配置（㉖：dev-redis 容器网络）
DEFINE_string(redis_host, "dev-redis", "Redis主机地址");
DEFINE_int32(redis_port, 6379, "Redis端口号");
DEFINE_string(redis_passwd, "123456", "Redis密码");
DEFINE_int32(redis_db, 0, "Redis数据库索引");
DEFINE_int32(redis_pool_size, 16, "Redis连接池大小");
// FastDFS配置（真实 tracker：compose 中的 dev-tracker 容器）
DEFINE_string(fdfs_trackers, "dev-tracker:22122", "FastDFS tracker地址（逗号分隔）");
DEFINE_int32(fdfs_connect_timeout, 30, "FastDFS连接超时时间");
DEFINE_int32(fdfs_network_timeout, 30, "FastDFS网络超时时间");
// 日志配置
DEFINE_bool(log_async, false, "是否启用异步日志");
DEFINE_int32(log_level, 2, "日志输出等级: 1-debug;2-info;3-warn;4-error;6-off");
DEFINE_string(log_format, "[%H:%M:%S][%-7l]: %v", "日志输出格式");
DEFINE_string(log_path, "stdout", "日志输出路径");
// 跨服务调用 key（fileBusiness.cc 使用，㉜两分法：调用 key 单列）
DEFINE_string(excel_service, "ExcelParserService", "Excel解析子服务调用key");
DEFINE_string(db_service, "DatabaseService", "数据库子服务调用key（等数据库子服务章节）");
// 需要监听的服务名称（发现上游：AI/Excel解析/数据库）
// 课件 StorageService 与用户子服务 watch 的 DatabaseService 不一致（㉜），统一后者
DEFINE_string(watch_ai_service, "AIService", "AI子服务名称（watch）");
DEFINE_string(watch_excel_service, "ExcelParserService", "Excel解析子服务名称（watch）");
DEFINE_string(watch_db_service, "DatabaseService", "数据库子服务名称（watch）");

void signalHandler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        exit(0);
    }
}

int main(int argc, char* argv[]) {
    // 1. 解析gflags参数
    google::ParseCommandLineFlags(&argc, &argv, true);
    // 2. 初始化日志器
    bitelog::log_settings logSettings;
    logSettings.async = FLAGS_log_async;
    logSettings.level = FLAGS_log_level;
    logSettings.format = FLAGS_log_format;
    logSettings.path = FLAGS_log_path;
    bitelog::bitelog_init(logSettings);
    INF("Bitelog initialized");
    // 3. 注册信号处理函数
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    INF("Signal handlers registered");
    // 4. 初始化数据库配置
    fileService::MysqlConfig mysqlConfig;
    mysqlConfig._host = FLAGS_mysql_host;
    mysqlConfig._user = FLAGS_mysql_user;
    mysqlConfig._passwd = FLAGS_mysql_passwd;
    mysqlConfig._db = FLAGS_mysql_db;
    mysqlConfig._port = FLAGS_mysql_port;
    mysqlConfig._cset = FLAGS_mysql_charset;
    // 5. 初始化Redis配置
    fileService::RedisConfig redisConfig;
    redisConfig._host = FLAGS_redis_host;
    redisConfig._port = FLAGS_redis_port;
    redisConfig._passwd = FLAGS_redis_passwd;
    redisConfig._db = FLAGS_redis_db;
    redisConfig._connectionPoolSize = FLAGS_redis_pool_size;
    // 6. 初始化FastDFS配置（按逗号拆分 tracker 列表，空段跳过）
    fileService::FastDFSConfig fastDFSConfig;
    {
        std::string item;
        std::istringstream iss(FLAGS_fdfs_trackers);
        while (std::getline(iss, item, ',')) {
            if (!item.empty()) {
                fastDFSConfig._trackers.push_back(item);
            }
        }
    }
    fastDFSConfig._connectTimeout = FLAGS_fdfs_connect_timeout;
    fastDFSConfig._networkTimeout = FLAGS_fdfs_network_timeout;
    // 7. 初始化注册中心配置
    fileService::RegisterCenterConfig registerCenterConfig;
    registerCenterConfig._etcdAddr = FLAGS_etcd_addr;
    registerCenterConfig._serviceName = FLAGS_service_name;
    registerCenterConfig._serviceAddr = FLAGS_service_addr;
    // 8. 初始化要监控的服务名称
    std::vector<std::string> watchServices;
    watchServices.push_back(FLAGS_watch_ai_service);
    watchServices.push_back(FLAGS_watch_excel_service);
    watchServices.push_back(FLAGS_watch_db_service);
    // 9. 构建各个实例，获取rpc服务器实例指针
    auto rpcServer = fileService::FileServerBuilder()
        .setMysqlConfig(mysqlConfig)
        .setRedisConfig(redisConfig)
        .setFastDFSConfig(fastDFSConfig)
        .setPort(FLAGS_port)
        .setEtcdAddr(FLAGS_etcd_addr)
        .setRegisterCenterConfig(registerCenterConfig)
        .setWatchServices(watchServices)
        .build();
    if (!rpcServer) {
        ERR("Failed to build FileServer");
        return -1;
    }
    // 10. 启动rpc服务器
    INF("FileService is running on port {}", FLAGS_port);
    rpcServer->start();
    return 0;
}

#include <signal.h>
#include <iostream>
#include <thread>
#include <gflags/gflags.h>
#include <bite_scaffold/log.h>
#include "userServer.h"   // 问题⑱修复：课件误写 userServerBuilder.h（不存在）

// RPC服务器配置
DEFINE_int32(port, 9001, "用户服务RPC端口");   // 问题⑯修复：课件重复 DEFINE 两次
// ETCD配置
DEFINE_string(etcd_addr, "http://dev-etcd:2379", "ETCD地址");
// 用户子服务配置（注册地址用容器网络名，网关经 dev-network 可达）
DEFINE_string(service_name, "UserService", "用户子服务名称");
DEFINE_string(service_addr, "dev-env-service:9001", "用户子服务地址");
// MySQL配置（课件原版 192.168.150.129 → 容器网络 dev-mysql）
DEFINE_string(mysql_host, "dev-mysql", "MySQL主机地址");
DEFINE_string(mysql_user, "root", "MySQL用户名");
DEFINE_string(mysql_passwd, "123456", "MySQL密码");
DEFINE_string(mysql_db, "chat2Data", "MySQL数据库名称");
DEFINE_int32(mysql_port, 3306, "MySQL端口号");
DEFINE_string(mysql_charset, "utf8mb4", "MySQL字符集");
// Redis配置
DEFINE_string(redis_host, "dev-redis", "Redis主机地址");
DEFINE_int32(redis_port, 6379, "Redis端口号");
DEFINE_string(redis_passwd, "123456", "Redis密码");
DEFINE_int32(redis_db, 0, "Redis数据库索引");
DEFINE_int32(redis_pool_size, 16, "Redis连接池大小");
// 要监听的服务名称
DEFINE_string(notify_services, "NotifyService", "需要监听的通知子服务名称");
DEFINE_string(database_services, "DatabaseService", "需要监听的数据库子服务名称");
// 日志配置
DEFINE_bool(log_async, false, "是否启用异步日志");
DEFINE_int32(log_level, 2, "日志输出等级: 1-debug;2-info;3-warn;4-error;6-off");
DEFINE_string(log_format, "[%H:%M:%S][%-7l]: %v", "日志输出格式");
DEFINE_string(log_path, "stdout", "日志输出路径");

void signalHandler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        exit(0);   // brpc 由 RunUntilAskedToQuit 管理，进程退出即释放
    }
}

int main(int argc, char* argv[]) {
    // 1. 解析命令行参数
    google::ParseCommandLineFlags(&argc, &argv, true);
    // 2. 初始化日志
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
    // 4. 配置MySQL
    biteodb::mysql_settings mysqlConfig;
    mysqlConfig.host = FLAGS_mysql_host;
    mysqlConfig.user = FLAGS_mysql_user;
    mysqlConfig.passwd = FLAGS_mysql_passwd;
    mysqlConfig.db = FLAGS_mysql_db;
    mysqlConfig.port = FLAGS_mysql_port;
    mysqlConfig.cset = FLAGS_mysql_charset;
    // 5. 配置Redis
    biteredis::redis_settings redisConfig;
    redisConfig.host = FLAGS_redis_host;
    redisConfig.port = FLAGS_redis_port;
    redisConfig.passwd = FLAGS_redis_passwd;
    redisConfig.db = FLAGS_redis_db;
    redisConfig.connection_pool_size = FLAGS_redis_pool_size;
    // 6. 配置要监听的服务名称（空串不监听）
    std::vector<std::string> watchServices;
    if (!FLAGS_notify_services.empty()) {
        watchServices.push_back(FLAGS_notify_services);
    }
    if (!FLAGS_database_services.empty()) {
        watchServices.push_back(FLAGS_database_services);
    }
    // 7. 构建RPC服务器以及其他配置
    auto rpcServer = userService::UserServerBuilder()
        .setMysqlConfig(mysqlConfig)
        .setRedisConfig(redisConfig)
        .setPort(FLAGS_port)
        .setEtcdAddr(FLAGS_etcd_addr)
        .setRegisterCenterConfig({FLAGS_etcd_addr, FLAGS_service_name, FLAGS_service_addr})
        .setWatchServices(watchServices)
        .build();
    // 问题⑰修复：课件此处误用 FLAGS_listen_port（网关的 flag 名），统一为 FLAGS_port
    INF("serviceName : {}, serviceAddr: {}, port: {}", FLAGS_service_name, FLAGS_service_addr, FLAGS_port);
    if (!rpcServer) {
        ERR("Failed to build UserServer");
        return -1;
    }
    // 8. 启动RPC服务器（阻塞直至退出信号）
    INF("UserServer is running on port {}", FLAGS_port);
    rpcServer->start();
    return 0;
}

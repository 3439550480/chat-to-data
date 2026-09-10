#include <signal.h>
#include <iostream>
#include <sstream>
#include <gflags/gflags.h>
#include <bite_scaffold/log.h>
#include "dbServer.h"

// ETCD地址
DEFINE_string(etcd_addr, "http://dev-etcd:2379", "ETCD地址");
// 数据库子服务配置（课件问题㊽修复：课件 main.cc 是文件服务复制品，端口/服务名/日志全错）
DEFINE_int32(port, 9005, "数据库服务RPC端口");
DEFINE_string(service_name, "DatabaseService", "数据库子服务名称");
DEFINE_string(service_addr, "dev-env-service:9005", "数据库子服务地址");
// MySQL配置（默认连接 excel_default 所用；容器网络）
DEFINE_string(mysql_host, "dev-mysql", "MySQL主机地址");
DEFINE_string(mysql_user, "root", "MySQL用户名");
DEFINE_string(mysql_passwd, "123456", "MySQL密码");
DEFINE_string(mysql_db, "chat2Data", "MySQL数据库名称");
DEFINE_int32(mysql_port, 3306, "MySQL端口号");
DEFINE_string(mysql_charset, "utf8mb4", "MySQL字符集");
// FastDFS配置（下载用户上传的 SQLite 文件用）
DEFINE_string(fdfs_trackers, "dev-tracker:22122", "FastDFS tracker地址（逗号分隔）");
DEFINE_int32(fdfs_connect_timeout, 30, "FastDFS连接超时时间");
DEFINE_int32(fdfs_network_timeout, 30, "FastDFS网络超时时间");
// 跨服务调用 key（与 dbBusiness.cc 的 DECLARE_string(file_service) 配对）
DEFINE_string(file_service, "FileService", "文件子服务调用key");
// 需要监听的服务名称（DB 服务只依赖文件服务：SQLite 文件下载）
DEFINE_string(watch_file_service, "FileService", "文件子服务名称（watch）");
// 日志配置
DEFINE_bool(log_async, false, "是否启用异步日志");
DEFINE_int32(log_level, 2, "日志输出等级: 1-debug;2-info;3-warn;4-error;6-off");
DEFINE_string(log_format, "[%H:%M:%S][%-7l]: %v", "日志输出格式");
DEFINE_string(log_path, "stdout", "日志输出路径");

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
    // 4. 初始化MySQL配置（供默认连接使用）
    databaseService::MysqlConfig mysqlConfig;
    mysqlConfig._host = FLAGS_mysql_host;
    mysqlConfig._user = FLAGS_mysql_user;
    mysqlConfig._passwd = FLAGS_mysql_passwd;
    mysqlConfig._db = FLAGS_mysql_db;
    mysqlConfig._port = FLAGS_mysql_port;
    mysqlConfig._cset = FLAGS_mysql_charset;
    mysqlConfig._connectionPoolSize = 16;
    // 5. 初始化FastDFS配置（按逗号拆分，空段跳过）
    databaseService::FastDFSConfig fastDFSConfig;
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
    // 6. 初始化注册中心配置
    databaseService::RegisterCenterConfig registerCenterConfig;
    registerCenterConfig._etcdAddr = FLAGS_etcd_addr;
    registerCenterConfig._serviceName = FLAGS_service_name;
    registerCenterConfig._serviceAddr = FLAGS_service_addr;
    // 7. 初始化要监控的服务名称（只 watch FileService）
    std::vector<std::string> watchServices;
    watchServices.push_back(FLAGS_watch_file_service);
    // 8. 构建各个实例，获取rpc服务器实例指针
    auto rpcServer = databaseService::DBServerBuilder()
        .setMysqlConfig(mysqlConfig)
        .setPort(FLAGS_port)
        .setEtcdAddr(FLAGS_etcd_addr)
        .setRegisterCenterConfig(registerCenterConfig)
        .setFastDFSConfig(fastDFSConfig)
        .setWatchServices(watchServices)
        .build();
    if (!rpcServer) {
        ERR("Failed to build DBServer");
        return -1;
    }
    // 9. 启动rpc服务器（阻塞直至退出信号）
    INF("DatabaseService is running on port {}", FLAGS_port);
    rpcServer->start();
    return 0;
}

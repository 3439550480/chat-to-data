#pragma once
#include <string>
#include <memory>
#include <vector>
#include <brpc/server.h>
#include <bite_scaffold/rpc.h>
#include <bite_scaffold/etcd.h>
#include "dbServiceImpl.h"

namespace bitesvc {
class SvcWatcher;
class SvcProvider;
}

namespace databaseService {

// MySQL 配置结构体（Builder 入参；与驱动层 MySQLConfig 字段名不同，build 内转换）
struct MysqlConfig {
    std::string _host;                 // 数据库主机地址
    std::string _user;                 // 数据库用户名
    std::string _passwd;               // 数据库密码
    std::string _db;                   // 数据库名称
    unsigned int _port;                // 数据库端口号
    std::string _cset;                 // 数据库字符集
    unsigned int _connectionPoolSize;  // 数据库连接池大小
};

// 注册中心配置结构体
struct RegisterCenterConfig {
    std::string _etcdAddr;            // 注册中心地址
    std::string _serviceName;         // 服务名称
    std::string _serviceAddr;         // 服务地址
};

// FastDFS 配置结构体（课件 int 无默认值 → 补默认值，同 ㉙ 系列）
struct FastDFSConfig {
    std::vector<std::string> _trackers = {};  // FastDFS Tracker 列表
    int _connectTimeout = 30;                 // 连接超时时间（秒）
    int _networkTimeout = 30;                 // 网络超时时间（秒）
};

class DBServer {
public:
    explicit DBServer(std::shared_ptr<DBServiceImpl> serviceImpl,
                      std::shared_ptr<brpc::Server> server,
                      std::shared_ptr<bitesvc::SvcWatcher> serviceWatcher,
                      std::shared_ptr<bitesvc::SvcProvider> serviceProvider);
    ~DBServer();
    // 启动数据库服务（阻塞直至退出信号）
    void start();
private:
    std::shared_ptr<brpc::Server> _server;                      // BRPC服务器
    std::shared_ptr<DBServiceImpl> _serviceImpl;                // 接口层实例
    std::shared_ptr<bitesvc::SvcWatcher> _serviceWatcher;       // 服务监控实例
    std::shared_ptr<bitesvc::SvcProvider> _serviceProvider;     // 服务注册实例
};

class DBServerBuilder {
public:
    ~DBServerBuilder();
    // 设置MySQL配置
    DBServerBuilder& setMysqlConfig(const MysqlConfig& mysqlConfig);
    // 设置端口号
    DBServerBuilder& setPort(int port);
    // 设置注册中心地址
    DBServerBuilder& setEtcdAddr(const std::string& etcdAddr);
    // 设置注册信息
    DBServerBuilder& setRegisterCenterConfig(const RegisterCenterConfig& registerCenterConfig);
    // 设置FastDFS配置
    DBServerBuilder& setFastDFSConfig(const FastDFSConfig& fastDFSConfig);
    // 设置监控服务列表
    DBServerBuilder& setWatchServices(const std::vector<std::string>& serviceNames);
    // 构建各个实例并返回RPC服务器对象
    std::shared_ptr<DBServer> build();
private:
    MysqlConfig _mysqlConfig;                            // MySQL配置
    int _port = 0;                                       // 端口号
    RegisterCenterConfig _registerCenterConfig;          // 注册中心配置
    FastDFSConfig _fastDFSConfig;                        // FastDFS配置
    std::vector<std::string> _watchServices;             // 监控服务列表
    std::shared_ptr<biterpc::SvcChannels> _svcChannels;  // 信道管理器
};

} // namespace databaseService

#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <mutex>
#include <vector>
#include <bite_scaffold/odb.h>
#include <bite_scaffold/rpc.h>
#include <bite_scaffold/redis.h>
#include <bite_scaffold/log.h>
#include <brpc/server.h>
#include "userServiceImpl.h"
#include "common.h"
#include "userBusiness.h"

namespace bitesvc {
class SvcWatcher;
class SvcProvider;   // 前向声明：成员用 shared_ptr，.cc 里才需要完整定义
}

namespace userService {

// RPC 服务器类：持有 brpc 服务、服务发现、服务注册三个运行时组件
class UserServer {
public:
    explicit UserServer(std::shared_ptr<UserServiceImpl> serviceImpl,
                        std::shared_ptr<brpc::Server> server,
                        std::shared_ptr<bitesvc::SvcWatcher> serviceWatcher,
                        std::shared_ptr<bitesvc::SvcProvider> serviceProvider);
    ~UserServer();
    // 启动RPC服务器（阻塞直至被要求退出）
    void start();
private:
    std::shared_ptr<brpc::Server> _server;                  // RPC服务器
    std::shared_ptr<UserServiceImpl> _serviceImpl;          // RPC接口定义实例
    std::shared_ptr<bitesvc::SvcWatcher> _serviceWatcher;   // 服务发现对象
    std::shared_ptr<bitesvc::SvcProvider> _serviceProvider; // 服务注册对象
};

struct registerCenterConfig {
    std::string _etcdAddr;       // etcd地址
    std::string _serviceName;    // 服务名称
    std::string _serviceAddr;    // 服务地址
};

// Builder：装配 MySQL/Redis 连接、业务层、RPC 服务、ETCD 注册与发现
class UserServerBuilder {
public:
    ~UserServerBuilder() {
        INF("UserServerBuilder destroyed");   // 本头文件已 include log.h，内联析构用 INF 安全
    }
    UserServerBuilder& setMysqlConfig(const biteodb::mysql_settings& mysqlConfig);
    UserServerBuilder& setRedisConfig(const biteredis::redis_settings& redisConfig);
    UserServerBuilder& setPort(int port);
    UserServerBuilder& setEtcdAddr(const std::string& etcdAddr);
    UserServerBuilder& setRegisterCenterConfig(const registerCenterConfig& registerCenterConfig);
    UserServerBuilder& setWatchServices(const std::vector<std::string>& serviceNames);
    // 构建UserServer
    std::shared_ptr<UserServer> build();
private:
    biteodb::mysql_settings _mysqlConfig;           // MySQL配置
    biteredis::redis_settings _redisConfig;         // Redis配置
    int _port;                                      // RPC服务器端口号
    registerCenterConfig _registerCenterConfig;     // 注册中心配置
    std::shared_ptr<UserServer> _server;            // UserServer实例
    std::string _etcdAddr;                          // etcd地址
    std::vector<std::string> _watchServices;        // 要监听的服务名称
    std::shared_ptr<biterpc::SvcChannels> _svcChannels; // 服务通道
};

} // namespace userService

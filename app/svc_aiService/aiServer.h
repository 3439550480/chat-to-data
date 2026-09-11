#pragma once
#include <string>
#include <memory>
#include <vector>
#include <atomic>
#include <mutex>
#include <brpc/server.h>
#include <bite_scaffold/rpc.h>
#include <bite_scaffold/etcd.h>
#include <ai_chat_sdk/ChatSDK.h>
#include "aiServiceImpl.h"
#include "aiBusiness.h"
#include "chatSessionMgr.h"
#include "common.h"
#include "../data/chatSessionData.h"

namespace bitesvc {
class SvcWatcher;
class SvcProvider;
}

namespace aiService {

// AI 服务运行时顶点：持 RPC 服务器 + watcher + provider
class AIServiceServer {
public:
    explicit AIServiceServer(std::shared_ptr<AIServiceImpl> serviceImpl,
                             std::shared_ptr<brpc::Server> server,
                             std::shared_ptr<bitesvc::SvcWatcher> serviceWatcher,
                             std::shared_ptr<bitesvc::SvcProvider> serviceProvider);
    ~AIServiceServer();
    // 启动 RPC 服务器（阻塞直至退出信号）
    void start();
private:
    std::shared_ptr<brpc::Server> _server;                    // RPC服务器实例
    std::shared_ptr<AIServiceImpl> _serviceImpl;              // 接口层实例
    std::shared_ptr<bitesvc::SvcWatcher> _serviceWatcher;     // 服务监听实例
    std::shared_ptr<bitesvc::SvcProvider> _serviceProvider;   // 服务注册实例
};

// 服务器构建器（课件 builder() 更名 build()，与其他服务一致——AI6）
class AIServiceBuilder {
public:
    ~AIServiceBuilder();
    // 设置MySQL配置
    AIServiceBuilder& setMysqlConfig(const MysqlConfig& mysqlConfig);
    // 设置Redis配置
    AIServiceBuilder& setRedisConfig(const RedisConfig& redisConfig);
    // 设置RPC服务器端口
    AIServiceBuilder& setPort(int port);
    // 设置ETCD地址
    AIServiceBuilder& setEtcdAddr(const std::string& etcdAddr);
    // 设置注册中心配置
    AIServiceBuilder& setRegisterCenterConfig(const RegisterCenterConfig& registerCenterConfig);
    // 设置ChatSDK配置（模型列表）
    AIServiceBuilder& setChatSDKConfig(const std::vector<std::shared_ptr<ai_chat_sdk::Config>>& modelConfigs);
    // 设置需要监听的服务名称
    AIServiceBuilder& setWatchServices(const std::vector<std::string>& serviceNames);
    // 构建各个实例，并返回RPC服务器实例指针
    std::shared_ptr<AIServiceServer> build();
private:
    MysqlConfig _mysqlConfig;                          // MySQL配置
    RedisConfig _redisConfig;                          // Redis配置
    int _port = 0;                                     // RPC服务器端口
    RegisterCenterConfig _registerCenterConfig;        // 注册中心配置
    std::vector<std::shared_ptr<ai_chat_sdk::Config>> _modelConfigs;  // ChatSDK模型配置
    std::vector<std::string> _watchServices;           // 需要监听的服务名称
    std::shared_ptr<biterpc::SvcChannels> _svcChannels;  // channel管理器实例
    std::shared_ptr<AIServiceServer> _server;          // RPC服务器实例
};

} // namespace aiService

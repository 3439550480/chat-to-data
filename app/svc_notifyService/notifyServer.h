#pragma once
#include <string>
#include <memory>
#include <brpc/server.h>
#include "notifyServiceImpl.h"
#include "emailSender.h"

namespace bitesvc {
class SvcProvider;
}

namespace notifyService {

// 注册中心配置结构
struct registerCenterConfig {
    std::string _etcdAddr;        // etcd服务器地址
    std::string _serviceName;     // 服务名称
    std::string _serviceAddr;     // 服务地址
};

// rpc服务器类
class NotifyServer {
public:
    explicit NotifyServer(std::shared_ptr<NotifyServiceImpl> serviceImpl,
                          std::shared_ptr<brpc::Server> server,
                          std::shared_ptr<bitesvc::SvcProvider> serviceProvider);
    ~NotifyServer();
    // 启动rpc服务器（阻塞直至退出信号）
    void start();
private:
    std::shared_ptr<brpc::Server> _server;                     // rpc服务器
    std::shared_ptr<NotifyServiceImpl> _serviceImpl;           // 通知服务接口实现
    std::shared_ptr<bitesvc::SvcProvider> _serviceProvider;    // 服务注册对象
};

// rpc服务器构建器类
class NotifyServerBuilder {
public:
    ~NotifyServerBuilder();
    // 设置rpc服务器端口号
    NotifyServerBuilder& setPort(int port);
    // 设置邮箱配置
    NotifyServerBuilder& setMailConfig(const mail_settings& mailConfig);
    // 设置注册中心配置
    NotifyServerBuilder& setRegisterCenterConfig(const registerCenterConfig& config);
    // 构建rpc服务器对象
    std::shared_ptr<NotifyServer> build();
private:
    int _port;                                    // rpc服务器端口号
    mail_settings _mailConfig;                    // 邮箱配置
    registerCenterConfig _registerCenterConfig;   // 注册中心配置
};

} // namespace notifyService

#include "notifyServer.h"
#include <bite_scaffold/log.h>
#include <bite_scaffold/rpc.h>
#include <bite_scaffold/etcd.h>

namespace notifyService {

NotifyServer::NotifyServer(std::shared_ptr<NotifyServiceImpl> serviceImpl,
                           std::shared_ptr<brpc::Server> server,
                           std::shared_ptr<bitesvc::SvcProvider> serviceProvider)
    : _serviceImpl(serviceImpl)
    , _server(server)
    , _serviceProvider(serviceProvider) {
    INF("NotifyServer initialized");
}

NotifyServer::~NotifyServer() {
    INF("NotifyServer destroyed");
}

void NotifyServer::start() {
    // brpc 阻塞运行直至退出信号（与 UserServer 同一模型）
    _server->RunUntilAskedToQuit();
}

NotifyServerBuilder::~NotifyServerBuilder() {
    INF("NotifyServerBuilder destroyed");
}

NotifyServerBuilder& NotifyServerBuilder::setPort(int port) {
    _port = port;
    return *this;
}

NotifyServerBuilder& NotifyServerBuilder::setMailConfig(const mail_settings& mailConfig) {
    _mailConfig = mailConfig;
    return *this;
}

NotifyServerBuilder& NotifyServerBuilder::setRegisterCenterConfig(const registerCenterConfig& config) {
    _registerCenterConfig = config;
    return *this;
}

std::shared_ptr<NotifyServer> NotifyServerBuilder::build() {
    // 1. 构建业务层对象（构造即 curl_global_init），并启动邮件发送线程
    auto notifyBusiness = std::make_shared<NotifyBusiness>(_mailConfig);
    notifyBusiness->start();
    // 2. 构建服务接口实现对象
    auto serviceImpl = std::make_shared<NotifyServiceImpl>(notifyBusiness);
    // 3. 构建brpc服务器（RpcServerFactory 注册 serviceImpl 并监听端口）
    auto rpcServer = biterpc::RpcServerFactory::create(_port, serviceImpl.get());
    if (!rpcServer) {
        ERR("Failed to create RPC server");
        return nullptr;
    }
    // 4. 构建注册中心对象，并向 ETCD 注册自己（供其他子服务发现）
    auto provider = std::make_shared<bitesvc::SvcProvider>(
        _registerCenterConfig._etcdAddr,
        _registerCenterConfig._serviceName,
        _registerCenterConfig._serviceAddr);
    if (!provider->registry()) {
        ERR("Failed to registry service: {}", _registerCenterConfig._serviceName);
        return nullptr;
    }
    // 5. 组装 NotifyServer 对象（课件步骤5注释复制错误已修正）
    auto server = std::make_shared<NotifyServer>(serviceImpl, rpcServer, provider);
    return server;
}

} // namespace notifyService

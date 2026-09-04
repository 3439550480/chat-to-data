#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <httplib.h>
#include <bite_scaffold/rpc.h>
#include "gatewayServiceImpl.h"

namespace GatewayService {

class GatewayServerBuilder;

// 网关服务器类: 负责HTTP服务器的启动、停止、路由绑定等核心功能
class GatewayServer {
public:
    GatewayServer(const std::string& host, int port);
    ~GatewayServer();

    // 启动网关服务器
    bool start();
    // 停止网关服务器
    void stop();
    // 检查服务器是否正在运行
    bool isRunning() const;
    // 绑定路由，将HTTP请求路由到对应的处理函数
    void bindRoutes();
    // 设置HTTP服务器实例
    void setServer(std::unique_ptr<httplib::Server> server);
    // 设置网关服务实现实例
    void setServiceImpl(std::unique_ptr<GatewayServiceImpl> serviceImpl);

    friend class GatewayServerBuilder;
private:
    std::string _host;                                // 服务器地址
    int _port;                                        // 服务器端口
    std::unique_ptr<httplib::Server> _server;         // http服务器实例
    std::unique_ptr<GatewayServiceImpl> _serviceImpl; // 接口层实例
    std::atomic<bool> _isRunning;                     // 检测服务器是否正常运行
    std::mutex _mutex;
};

// 网关服务器构建器类: 使用Builder模式构建网关服务器，配置服务器地址、ETCD服务发现等参数
// 课件原版析构函数内联在头文件中且使用 INF 宏，但本头文件未 include log.h——
// 依赖包含者的包含顺序才能编译，属脆弱设计；修复：此处只声明，实现移至 gatewayServer.cc
class GatewayServerBuilder {
public:
    ~GatewayServerBuilder();
    // 设置网关服务器监听地址
    GatewayServerBuilder& setHost(const std::string& host);
    // 设置网关服务器监听端口
    GatewayServerBuilder& setPort(int port);
    // 设置ETCD注册中心地址
    GatewayServerBuilder& setEtcdAddr(const std::string& etcdAddr);
    // 添加需要发现的服务名称
    GatewayServerBuilder& addService(const std::string& serviceName);
    // 构建并启动网关服务器
    std::shared_ptr<GatewayServer> build();

private:
    // 初始化服务发现组件
    void initServiceDiscovery();

private:
    std::string _host;                                    // 服务器地址
    int _port;                                            // 服务器端口
    std::string _etcdAddr;                                // ETCD地址
    std::vector<std::string> _serviceNames;               // 服务名称列表
    std::shared_ptr<biterpc::SvcChannels> _svcChannels;   // 服务信道
    std::shared_ptr<bitesvc::SvcWatcher> _serviceWatcher; // 服务监控
    std::shared_ptr<GatewayServer> _server;               // 网关服务器实例
};

} // end GatewayService

#include "userServer.h"
#include <bite_scaffold/etcd.h>
#include <thread>

namespace userService {

UserServer::UserServer(std::shared_ptr<UserServiceImpl> serviceImpl,
                       std::shared_ptr<brpc::Server> server,
                       std::shared_ptr<bitesvc::SvcWatcher> serviceWatcher,
                       std::shared_ptr<bitesvc::SvcProvider> serviceProvider)
    : _serviceImpl(serviceImpl)
    , _server(server)
    , _serviceWatcher(serviceWatcher)
    , _serviceProvider(serviceProvider) {
    INF("UserServer initialized");
}

UserServer::~UserServer() {
    INF("UserServer destroyed");
}

// RunUntilAskedToQuit：阻塞当前线程直至 brpc 收到退出请求——与网关的
// "独立线程 listen + 主循环轮询"不同，brpc 直接占用 main 线程，Ctrl+C 即退
void UserServer::start() {
    _server->RunUntilAskedToQuit();
}

// 初始化MySQL配置
UserServerBuilder& UserServerBuilder::setMysqlConfig(const biteodb::mysql_settings& mysqlConfig) {
    _mysqlConfig = mysqlConfig;
    return *this;
}
// 初始化Redis配置
UserServerBuilder& UserServerBuilder::setRedisConfig(const biteredis::redis_settings& redisConfig) {
    _redisConfig = redisConfig;
    return *this;
}
// 设置RPC服务器端口
UserServerBuilder& UserServerBuilder::setPort(int port) {
    _port = port;
    return *this;
}
// 设置ETCD服务器地址
UserServerBuilder& UserServerBuilder::setEtcdAddr(const std::string& etcdAddr) {
    _etcdAddr = etcdAddr;
    return *this;
}
// 设置注册中心配置
UserServerBuilder& UserServerBuilder::setRegisterCenterConfig(const registerCenterConfig& registerCenterConfig) {
    _registerCenterConfig = registerCenterConfig;
    return *this;
}
// 设置要监控的服务名称
UserServerBuilder& UserServerBuilder::setWatchServices(const std::vector<std::string>& serviceNames) {
    _watchServices = serviceNames;
    return *this;
}

std::shared_ptr<UserServer> UserServerBuilder::build() {
    // 1. 创建channel的管理器，并为每个要监听的服务建立节点集合
    _svcChannels = std::make_shared<biterpc::SvcChannels>();
    for (const auto& serviceName : _watchServices) {
        _svcChannels->setWatch(serviceName);
        INF("Set watch for service: {}", serviceName);
    }
    // 2. 创建业务逻辑层（连接工厂 → 数据层三件套 → SessionManager → UserBusiness）
    std::shared_ptr<odb::database> mysql = biteodb::DBFactory::mysql(_mysqlConfig);
    std::shared_ptr<sw::redis::Redis> redis = biteredis::RedisFactory::create(_redisConfig);
    std::shared_ptr<UserData> userData = std::make_shared<UserData>(mysql, redis);
    std::shared_ptr<SessionData> sessionData = std::make_shared<SessionData>(mysql, redis);
    std::shared_ptr<VerifyCodeData> verifyCodeData = std::make_shared<VerifyCodeData>(redis);
    std::shared_ptr<SessionManager> sessionManager = std::make_shared<SessionManager>(sessionData.get());
    std::shared_ptr<UserBusiness> userBusiness = std::make_shared<UserBusiness>(
        sessionManager,
        verifyCodeData,
        userData,
        _svcChannels
    );
    // 3. 创建RPC接口定义实例
    auto serviceImpl = std::make_shared<UserServiceImpl>(userBusiness);
    // 4. 创建RPC服务器（脚手架工厂：注册 service + 启动参数）
    auto rpcServer = biterpc::RpcServerFactory::create(_port, serviceImpl.get());
    if (!rpcServer) {
        ERR("Failed to create RPC server");
        return nullptr;
    }
    // 5. 服务发现：回调把其他子服务的上下线实时同步进信道表（与网关同机制，方向相反——
    //    网关是"发现方"，本服务既发现别人（notify/db）也被别人发现（网关））
    // 5.1 创建服务上线 和 服务下线的回调
    auto onlineCallback = [this](const std::string& serviceName, const std::string& serviceAddr) {
        INF("Service online: {} at {}", serviceName, serviceAddr);
        _svcChannels->addNode(serviceName, serviceAddr);
    };
    auto offlineCallback = [this](const std::string& serviceName, const std::string& serviceAddr) {
        INF("Service offline: {} at {}", serviceName, serviceAddr);
        _svcChannels->delNode(serviceName, serviceAddr);
    };
    // 5.2 创建服务器发现对象
    INF("Watch etcd addr: {}", _etcdAddr);
    std::shared_ptr<bitesvc::SvcWatcher> watcher =
        std::make_shared<bitesvc::SvcWatcher>(_etcdAddr, onlineCallback, offlineCallback);
    // 5.3 开启服务发现（watch 阻塞常驻，detach 与服务器同寿）
    std::thread watcherThread([watcher]() {
        watcher->watch();
    });
    watcherThread.detach();
    // 6. 服务注册：把自己（service_name/service_addr）写入 ETCD，让网关能发现本服务
    INF("Register etcd addr: {}", _registerCenterConfig._etcdAddr);
    auto provider = std::make_shared<bitesvc::SvcProvider>(
        _registerCenterConfig._etcdAddr,
        _registerCenterConfig._serviceName,
        _registerCenterConfig._serviceAddr);
    if (!provider->registry()) {
        ERR("Failed to registry service: {}", _registerCenterConfig._serviceName);
        return nullptr;
    }
    // 7. 创建UserServer实例（接管 serviceImpl/rpcServer/watcher/provider 的生命周期）
    auto server = std::make_shared<UserServer>(serviceImpl, rpcServer, watcher, provider);
    // 8. 返回RPC服务器对象
    return server;
}

} // namespace userService

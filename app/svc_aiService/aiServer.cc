#include <thread>
#include <bite_scaffold/log.h>
#include <bite_scaffold/rpc.h>
#include <bite_scaffold/odb.h>
#include <bite_scaffold/redis.h>
#include <ai_chat_sdk/ChatSDK.h>
#include "aiServer.h"

namespace aiService {

////////////////////////////// AIServiceServer 实现 //////////////////////////////

AIServiceServer::AIServiceServer(std::shared_ptr<AIServiceImpl> serviceImpl,
                                 std::shared_ptr<brpc::Server> server,
                                 std::shared_ptr<bitesvc::SvcWatcher> serviceWatcher,
                                 std::shared_ptr<bitesvc::SvcProvider> serviceProvider)
    : _serviceImpl(serviceImpl)
    , _server(server)
    , _serviceWatcher(serviceWatcher)
    , _serviceProvider(serviceProvider) {
    INF("AIServiceServer initialized");
}

AIServiceServer::~AIServiceServer() {
    INF("AIServiceServer destroyed");
}

void AIServiceServer::start() {
    if (_server) {
        INF("AIServiceServer is running (RunUntilAskedToQuit)");
        _server->RunUntilAskedToQuit();
        // 课件笔误修正：原 INF 写在阻塞调用之后，退出时才打印
    }
}

////////////////////////////// AIServiceBuilder 实现 //////////////////////////////

AIServiceBuilder::~AIServiceBuilder() {
    INF("AIServiceBuilder destroyed");
}

AIServiceBuilder& AIServiceBuilder::setMysqlConfig(const MysqlConfig& mysqlConfig) {
    _mysqlConfig = mysqlConfig;
    return *this;
}

AIServiceBuilder& AIServiceBuilder::setRedisConfig(const RedisConfig& redisConfig) {
    _redisConfig = redisConfig;
    return *this;
}

AIServiceBuilder& AIServiceBuilder::setPort(int port) {
    _port = port;
    return *this;
}

AIServiceBuilder& AIServiceBuilder::setEtcdAddr(const std::string& etcdAddr) {
    _registerCenterConfig._etcdAddr = etcdAddr;
    return *this;
}

AIServiceBuilder& AIServiceBuilder::setRegisterCenterConfig(
    const RegisterCenterConfig& registerCenterConfig) {
    _registerCenterConfig = registerCenterConfig;
    return *this;
}

AIServiceBuilder& AIServiceBuilder::setChatSDKConfig(
    const std::vector<std::shared_ptr<ai_chat_sdk::Config>>& modelConfigs) {
    _modelConfigs = modelConfigs;
    return *this;
}

AIServiceBuilder& AIServiceBuilder::setWatchServices(const std::vector<std::string>& serviceNames) {
    _watchServices = serviceNames;
    return *this;
}

std::shared_ptr<AIServiceServer> AIServiceBuilder::build() {
    // 1. 初始化 ChatSDK（GLM 模型经 .env → main.cc 传入；
    //    initModels 内部会打日志 → main.cc 已提前 initLogger，否则段错误）
    auto chatSdk = std::make_shared<ai_chat_sdk::ChatSDK>();
    if (!chatSdk->initModels(_modelConfigs)) {
        ERR("Failed to initialize ChatSDK models");
        return nullptr;
    }
    INF("ChatSDK initialized with {} models", _modelConfigs.size());
    // 2. 信道管理器 + watch（四条依赖线：worksheet 表名/SQL 执行/用户邮箱/发邮件）
    _svcChannels = std::make_shared<biterpc::SvcChannels>();
    for (const auto& serviceName : _watchServices) {
        _svcChannels->setWatch(serviceName);
        INF("Set watch for service: {}", serviceName);
    }
    // 3. MySQL（ODB：tbl_chatSession 的持久化通道）
    biteodb::mysql_settings mysqlSettings;
    mysqlSettings.host = _mysqlConfig._host;
    mysqlSettings.user = _mysqlConfig._user;
    mysqlSettings.passwd = _mysqlConfig._passwd;
    mysqlSettings.db = _mysqlConfig._db;
    mysqlSettings.port = _mysqlConfig._port;
    mysqlSettings.cset = _mysqlConfig._cset;
    mysqlSettings.connection_pool_size = _mysqlConfig._connectionPoolSize;
    std::shared_ptr<odb::database> mysql = biteodb::DBFactory::mysql(mysqlSettings);
    if (!mysql) {
        ERR("Failed to connect MySQL");
        return nullptr;
    }
    INF("MySQL connected (odb)");
    // 4. Redis（会话元数据缓存）
    biteredis::redis_settings redisSettings;
    redisSettings.host = _redisConfig._host;
    redisSettings.port = _redisConfig._port;
    redisSettings.passwd = _redisConfig._passwd;
    redisSettings.db = _redisConfig._db;
    redisSettings.connection_pool_size = _redisConfig._connectionPoolSize;
    std::shared_ptr<sw::redis::Redis> redis = biteredis::RedisFactory::create(redisSettings);
    if (!redis) {
        ERR("Failed to connect Redis");
        return nullptr;
    }
    INF("Redis connected");
    // 5~8. 数据层 → 会话管理器 → 业务层 → 接口层
    auto chatSessionData = std::make_shared<ChatSessionData>(mysql, redis);
    auto chatSessionMgr = std::make_shared<ChatSessionMgr>(chatSessionData);
    auto aiBusiness = std::make_shared<AIBusiness>(chatSdk, chatSessionMgr, _svcChannels);
    auto serviceImpl = std::make_shared<AIServiceImpl>(aiBusiness);
    // 9. RPC 服务器
    auto rpcServer = biterpc::RpcServerFactory::create(_port, serviceImpl.get());
    if (!rpcServer) {
        ERR("Failed to create RPC server");
        return nullptr;
    }
    // 10. 监控回调 —— B1 修复：按值捕获 channels（Builder 是临时对象，
    //     build() 返回即析构，[this] 会在后续 etcd 事件中悬空）
    auto channels = _svcChannels;
    auto onlineCallback = [channels](const std::string& serviceName, const std::string& serviceAddr) {
        INF("Service online: {} at {}", serviceName, serviceAddr);
        channels->addNode(serviceName, serviceAddr);
    };
    auto offlineCallback = [channels](const std::string& serviceName, const std::string& serviceAddr) {
        INF("Service offline: {} at {}", serviceName, serviceAddr);
        channels->delNode(serviceName, serviceAddr);
    };
    // 11. watcher（detach 线程，与服务器同寿）
    INF("Watch etcd addr: {}", _registerCenterConfig._etcdAddr);
    auto watcher = std::make_shared<bitesvc::SvcWatcher>(
        _registerCenterConfig._etcdAddr, onlineCallback, offlineCallback);
    std::thread watcherThread([watcher]() {
        watcher->watch();
    });
    watcherThread.detach();
    // 12. 服务注册
    INF("Register etcd addr: {}", _registerCenterConfig._etcdAddr);
    auto provider = std::make_shared<bitesvc::SvcProvider>(
        _registerCenterConfig._etcdAddr,
        _registerCenterConfig._serviceName,
        _registerCenterConfig._serviceAddr);
    if (!provider->registry()) {
        ERR("Failed to registry service: {}", _registerCenterConfig._serviceName);
        return nullptr;
    }
    // 13~14. 组装并返回
    _server = std::make_shared<AIServiceServer>(serviceImpl, rpcServer, watcher, provider);
    INF("AIServiceServer built successfully");
    return _server;
}

} // namespace aiService

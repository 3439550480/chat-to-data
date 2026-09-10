#include <thread>
#include <bite_scaffold/log.h>
#include <bite_scaffold/rpc.h>
#include <bite_scaffold/odb.h>
#include "dbServer.h"
#include "dbConnMgr.h"
#include "dbBusiness.h"
#include "dbDriver/databaseFactory.h"
#include "dbDriver/databaseSchema.h"
#include "../common/errorHandler.h"
// 课件问题㉚纪律：pb.h 已随 dbServer.h→impl→business 拉入，fdfs.h 必须在其后 + 双保险
#include <bite_scaffold/fdfs.h>
#ifdef byte
#undef byte
#endif

namespace databaseService {

// ============================ DBServer ============================

DBServer::DBServer(std::shared_ptr<DBServiceImpl> serviceImpl,
                   std::shared_ptr<brpc::Server> server,
                   std::shared_ptr<bitesvc::SvcWatcher> serviceWatcher,
                   std::shared_ptr<bitesvc::SvcProvider> serviceProvider)
    : _serviceImpl(serviceImpl)
    , _server(server)
    , _serviceWatcher(serviceWatcher)
    , _serviceProvider(serviceProvider) {
    INF("DBServer initialized");
}

DBServer::~DBServer() {
    INF("DBServer destroyed");
}

void DBServer::start() {
    if (_server) {
        INF("DBServer is running (RunUntilAskedToQuit)");
        _server->RunUntilAskedToQuit();
        // 课件笔误修正：原 INF 写在阻塞调用之后，退出时才打印
    }
}

// ============================ DBServerBuilder ============================

DBServerBuilder::~DBServerBuilder() {
    INF("DBServerBuilder destroyed");
}

DBServerBuilder& DBServerBuilder::setMysqlConfig(const MysqlConfig& mysqlConfig) {
    _mysqlConfig = mysqlConfig;
    return *this;
}

DBServerBuilder& DBServerBuilder::setPort(int port) {
    _port = port;
    return *this;
}

DBServerBuilder& DBServerBuilder::setEtcdAddr(const std::string& etcdAddr) {
    _registerCenterConfig._etcdAddr = etcdAddr;
    return *this;
}

DBServerBuilder& DBServerBuilder::setRegisterCenterConfig(
    const RegisterCenterConfig& registerCenterConfig) {
    _registerCenterConfig = registerCenterConfig;
    return *this;
}

DBServerBuilder& DBServerBuilder::setFastDFSConfig(const FastDFSConfig& fastDFSConfig) {
    _fastDFSConfig = fastDFSConfig;
    return *this;
}

DBServerBuilder& DBServerBuilder::setWatchServices(const std::vector<std::string>& serviceNames) {
    _watchServices = serviceNames;
    return *this;
}

std::shared_ptr<DBServer> DBServerBuilder::build() {
    // 1. 初始化 FastDFS 客户端（trackers 为空时占位启动，同前两个服务）
    bitefdfs::fdfs_settings fdfsSettings;
    fdfsSettings.trackers = _fastDFSConfig._trackers;
    if (fdfsSettings.trackers.empty()) {
        WRN("FastDFS trackers 为空：使用占位 tracker 启动（SQLite 文件下载会失败但进程正常）");
        fdfsSettings.trackers.push_back("127.0.0.1:22122");
    }
    fdfsSettings.connect_timeout = _fastDFSConfig._connectTimeout;
    fdfsSettings.network_timeout = _fastDFSConfig._networkTimeout;
    bitefdfs::FDFSClient::init(fdfsSettings);
    INF("FastDFS client initialized (trackers={})", fdfsSettings.trackers.size());
    // 2. 初始化信道管理器并设置要监听的服务（按决策：只挂 FileService）
    _svcChannels = std::make_shared<biterpc::SvcChannels>();
    for (const auto& serviceName : _watchServices) {
        _svcChannels->setWatch(serviceName);
        INF("Set watch for service: {}", serviceName);
    }
    // 3. 连接管理器（进程级共享，有独立清理线程）
    auto connMgr = std::make_shared<DBConnMgr>();
    // 4. Builder 配置 → 驱动层配置（字段名不同，显式转换）
    MySQLConfig mysqlConf;
    mysqlConf.host = _mysqlConfig._host;
    mysqlConf.port = static_cast<int>(_mysqlConfig._port);
    mysqlConf.username = _mysqlConfig._user;
    mysqlConf.password = _mysqlConfig._passwd;
    mysqlConf.database = _mysqlConfig._db;
    mysqlConf.charset = _mysqlConfig._cset;
    // 5. 业务层（构造内建立默认 MySQL 连接 excel_default；失败则整体构建失败）
    std::shared_ptr<DBBusiness> dbBusiness;
    try {
        dbBusiness = std::make_shared<DBBusiness>(connMgr, _svcChannels, mysqlConf);
    } catch (const chat2Data::Chat2DataException& e) {
        ERR("Failed to build DBBusiness (default connection): {}", e.what());
        return nullptr;
    }
    // 6. 接口层
    auto serviceImpl = std::make_shared<DBServiceImpl>(dbBusiness);
    // 7. RPC 服务器
    auto rpcServer = biterpc::RpcServerFactory::create(_port, serviceImpl.get());
    if (!rpcServer) {
        ERR("Failed to create RPC server");
        return nullptr;
    }
    // 8. 服务监控回调：上游上线/下线 → 增删 channel 节点
    // B1 修复：按值捕获 channels（不用 [this]）——回调由 detach 的 watcher 线程长期持有，
    // 而 Builder 在 main 中是临时对象，build() 返回即析构，[this] 会在后续事件中悬空
    auto channels = _svcChannels;
    auto onlineCallback = [channels](const std::string& serviceName, const std::string& serviceAddr) {
        INF("Service online: {} at {}", serviceName, serviceAddr);
        channels->addNode(serviceName, serviceAddr);
    };
    auto offlineCallback = [channels](const std::string& serviceName, const std::string& serviceAddr) {
        INF("Service offline: {} at {}", serviceName, serviceAddr);
        channels->delNode(serviceName, serviceAddr);
    };
    // 9. 启动 watcher（detach 线程，与服务器同寿）
    INF("Watch etcd addr: {}", _registerCenterConfig._etcdAddr);
    auto watcher = std::make_shared<bitesvc::SvcWatcher>(
        _registerCenterConfig._etcdAddr, onlineCallback, offlineCallback);
    std::thread watcherThread([watcher]() {
        watcher->watch();
    });
    watcherThread.detach();
    // 10. 服务注册
    INF("Register etcd addr: {}", _registerCenterConfig._etcdAddr);
    auto provider = std::make_shared<bitesvc::SvcProvider>(
        _registerCenterConfig._etcdAddr,
        _registerCenterConfig._serviceName,
        _registerCenterConfig._serviceAddr);
    if (!provider->registry()) {
        ERR("Failed to registry service: {}", _registerCenterConfig._serviceName);
        return nullptr;
    }
    // 11. 组装并返回
    auto server = std::make_shared<DBServer>(serviceImpl, rpcServer, watcher, provider);
    INF("DBServer built successfully");
    return server;
}

} // namespace databaseService

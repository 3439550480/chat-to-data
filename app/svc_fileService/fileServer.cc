#include <thread>
#include <bite_scaffold/log.h>
#include <bite_scaffold/rpc.h>
#include <bite_scaffold/etcd.h>
#include <bite_scaffold/odb.h>
#include <bite_scaffold/redis.h>
#include "fileServer.h"
// 课件问题㉚纪律：pb.h 已随 fileServer.h→impl→business 拉入，fdfs.h 在其后 + 双保险
#include <bite_scaffold/fdfs.h>
#ifdef byte
#undef byte
#endif

namespace fileService {

FileServer::FileServer(std::shared_ptr<FileServiceImpl> serviceImpl,
                       std::shared_ptr<brpc::Server> server,
                       std::shared_ptr<bitesvc::SvcWatcher> serviceWatcher,
                       std::shared_ptr<bitesvc::SvcProvider> serviceProvider)
    : _serviceImpl(serviceImpl)
    , _server(server)
    , _serviceWatcher(serviceWatcher)
    , _serviceProvider(serviceProvider) {
    INF("FileServer initialized");
}

FileServer::~FileServer() {
    INF("FileServer destroyed");
}

void FileServer::start() {
    if (_server) {
        INF("FileServer is running (RunUntilAskedToQuit)");
        _server->RunUntilAskedToQuit();
        // 课件笔误修正：原 INF 在阻塞调用之后，退出时才会打印，已挪至前面
    }
}

FileServerBuilder::~FileServerBuilder() {
    INF("FileServerBuilder destroyed");
}

FileServerBuilder& FileServerBuilder::setMysqlConfig(const MysqlConfig& mysqlConfig) {
    _mysqlConfig = mysqlConfig;
    return *this;
}

FileServerBuilder& FileServerBuilder::setRedisConfig(const RedisConfig& redisConfig) {
    _redisConfig = redisConfig;
    return *this;
}

FileServerBuilder& FileServerBuilder::setPort(int port) {
    _port = port;
    return *this;
}

FileServerBuilder& FileServerBuilder::setEtcdAddr(const std::string& etcdAddr) {
    _registerCenterConfig._etcdAddr = etcdAddr;
    return *this;
}

FileServerBuilder& FileServerBuilder::setRegisterCenterConfig(
    const RegisterCenterConfig& config) {
    _registerCenterConfig = config;
    return *this;
}

FileServerBuilder& FileServerBuilder::setFastDFSConfig(const FastDFSConfig& fastDFSConfig) {
    _fastDFSConfig = fastDFSConfig;
    return *this;
}

FileServerBuilder& FileServerBuilder::setWatchServices(const std::vector<std::string>& serviceNames) {
    _watchServices = serviceNames;
    return *this;
}

std::shared_ptr<FileServer> FileServerBuilder::build() {
    // 1. 初始化FastDFS客户端（trackers 为空时占位启动，同 ExcelParserService 的降级策略）
    bitefdfs::fdfs_settings fdfsSettings;
    fdfsSettings.trackers = _fastDFSConfig._trackers;
    if (fdfsSettings.trackers.empty()) {
        WRN("FastDFS trackers 为空：使用占位 tracker 启动（下载请求会失败但进程正常）");
        fdfsSettings.trackers.push_back("127.0.0.1:22122");
    }
    fdfsSettings.connect_timeout = _fastDFSConfig._connectTimeout;
    fdfsSettings.network_timeout = _fastDFSConfig._networkTimeout;
    bitefdfs::FDFSClient::init(fdfsSettings);
    INF("FastDFS client initialized (trackers={})", fdfsSettings.trackers.size());
    // 2. 实例化channel管理器，并为每个要监听的服务建立节点集合
    _svcChannels = std::make_shared<biterpc::SvcChannels>();
    for (const auto& serviceName : _watchServices) {
        _svcChannels->setWatch(serviceName);
        INF("Set watch for service: {}", serviceName);
    }
    // 3. 连接MySQL
    biteodb::mysql_settings mysqlSettings;
    mysqlSettings.host = _mysqlConfig._host;
    mysqlSettings.user = _mysqlConfig._user;
    mysqlSettings.passwd = _mysqlConfig._passwd;
    mysqlSettings.db = _mysqlConfig._db;
    mysqlSettings.port = _mysqlConfig._port;
    mysqlSettings.cset = _mysqlConfig._cset;
    std::shared_ptr<odb::database> mysql = biteodb::DBFactory::mysql(mysqlSettings);
    // 4. 连接Redis
    biteredis::redis_settings redisSettings;
    redisSettings.host = _redisConfig._host;
    redisSettings.port = _redisConfig._port;
    redisSettings.passwd = _redisConfig._passwd;
    redisSettings.db = _redisConfig._db;
    redisSettings.connection_pool_size = _redisConfig._connectionPoolSize;
    std::shared_ptr<sw::redis::Redis> redis = biteredis::RedisFactory::create(redisSettings);
    // 5. 实例化数据层对象
    std::shared_ptr<FileInfoData> fileInfoData = std::make_shared<FileInfoData>(mysql, redis);
    std::shared_ptr<WorksheetData> worksheetData = std::make_shared<WorksheetData>(mysql, redis);
    // 6. 实例化业务层对象
    std::shared_ptr<FileBusiness> fileBusiness = std::make_shared<FileBusiness>(
        fileInfoData, worksheetData, _svcChannels);
    // 7. 实例化接口层对象
    std::shared_ptr<FileServiceImpl> serviceImpl = std::make_shared<FileServiceImpl>(fileBusiness);
    // 8. 实例化RPC服务器
    auto rpcServer = biterpc::RpcServerFactory::create(_port, serviceImpl.get());
    if (!rpcServer) {
        ERR("Failed to create RPC server");
        return nullptr;
    }
    // 9. 服务监控回调：上游服务上线/下线 → 增删 channel 节点
    auto onlineCallback = [this](const std::string& serviceName, const std::string& serviceAddr) {
        INF("Service online: {} at {}", serviceName, serviceAddr);
        _svcChannels->addNode(serviceName, serviceAddr);
    };
    auto offlineCallback = [this](const std::string& serviceName, const std::string& serviceAddr) {
        INF("Service offline: {} at {}", serviceName, serviceAddr);
        _svcChannels->delNode(serviceName, serviceAddr);
    };
    // 10. 实例化服务监控对象，并起 detach 线程开始 watch
    INF("Watch etcd addr: {}", _registerCenterConfig._etcdAddr);
    std::shared_ptr<bitesvc::SvcWatcher> watcher = std::make_shared<bitesvc::SvcWatcher>(
        _registerCenterConfig._etcdAddr, onlineCallback, offlineCallback);
    std::thread watcherThread([watcher]() {
        watcher->watch();
    });
    watcherThread.detach();
    // 11. 实例化服务注册对象，并向 ETCD 注册自己
    INF("Register etcd addr: {}", _registerCenterConfig._etcdAddr);
    auto provider = std::make_shared<bitesvc::SvcProvider>(
        _registerCenterConfig._etcdAddr,
        _registerCenterConfig._serviceName,
        _registerCenterConfig._serviceAddr);
    if (!provider->registry()) {
        ERR("Failed to registry service: {}", _registerCenterConfig._serviceName);
        return nullptr;
    }
    // 12. 组装 FileServer
    auto server = std::make_shared<FileServer>(serviceImpl, rpcServer, watcher, provider);
    INF("FileServer built successfully");
    return server;
}

} // namespace fileService

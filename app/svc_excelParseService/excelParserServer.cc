#include "excelParserServer.h"
#include <bite_scaffold/log.h>
#include <bite_scaffold/rpc.h>
#include <bite_scaffold/etcd.h>
// 课件问题㉚：本头文件已间接拉入 protobuf，fdfs 必须在其后 include；
// fastcommon 的 byte 宏再 #undef 双保险（同 excelParserBusiness.cc）
#include <bite_scaffold/fdfs.h>
#ifdef byte
#undef byte
#endif

namespace excelParserService {

// ==================== ExcelParserServer ====================

ExcelParserServer::ExcelParserServer(std::shared_ptr<ExcelParserServiceImpl> serviceImpl,
                                     std::shared_ptr<brpc::Server> server,
                                     std::shared_ptr<bitesvc::SvcProvider> serviceProvider)
    : _serviceImpl(serviceImpl)
    , _server(server)
    , _serviceProvider(serviceProvider) {
    INF("ExcelParserServer initialized");
}

ExcelParserServer::~ExcelParserServer() {
    INF("ExcelParserServer destroyed");
}

void ExcelParserServer::start() {
    // brpc 阻塞运行直至退出信号（与 UserServer/NotifyServer 同一模型）
    _server->RunUntilAskedToQuit();
}

// ==================== ExcelParserServerBuilder ====================

ExcelParserServerBuilder::~ExcelParserServerBuilder() {
    INF("ExcelParserServerBuilder destroyed");
}

ExcelParserServerBuilder& ExcelParserServerBuilder::setPort(int port) {
    _port = port;
    return *this;
}

ExcelParserServerBuilder& ExcelParserServerBuilder::setRegisterCenterConfig(
    const RegisterCenterConfig& config) {
    _registerCenterConfig = config;
    return *this;
}

ExcelParserServerBuilder& ExcelParserServerBuilder::setFastDFSConfig(
    const FastDFSConfig& fastDFSConfig) {
    _fastDFSConfig = fastDFSConfig;
    return *this;
}

std::shared_ptr<ExcelParserServer> ExcelParserServerBuilder::build() {
    // 1. 初始化FastDFS客户端（进程级一次）
    // 实测：fdfs_client_init_from_buffer 在无 tracker_server 项时直接失败
    // （脚手架 init 里 abort），因此 trackers 为空时注入占位 tracker 完成初始化——
    // 配置解析可过，连接失败推迟到请求时由业务层优雅转错误码；
    // 真实 tracker 等文件子服务章节部署 FastDFS 后通过 --fdfs_trackers 填入
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
    // 2. 创建业务层实例
    auto business = std::make_shared<ExcelParserBusiness>();
    // 3. 创建接口层实例
    auto serviceImpl = std::make_shared<ExcelParserServiceImpl>(business);
    // 4. 创建brpc服务器（RpcServerFactory 注册 serviceImpl 并监听端口）
    auto rpcServer = biterpc::RpcServerFactory::create(_port, serviceImpl.get());
    if (!rpcServer) {
        ERR("Failed to create RPC server");
        return nullptr;
    }
    // 5. 创建服务注册层实例，并向 ETCD 注册自己
    auto provider = std::make_shared<bitesvc::SvcProvider>(
        _registerCenterConfig._etcdAddr,
        _registerCenterConfig._serviceName,
        _registerCenterConfig._serviceAddr);
    if (!provider->registry()) {
        ERR("Failed to registry service: {}", _registerCenterConfig._serviceName);
        return nullptr;
    }
    // 6. 组装 ExcelParserServer 对象
    auto server = std::make_shared<ExcelParserServer>(serviceImpl, rpcServer, provider);
    return server;
}

} // namespace excelParserService

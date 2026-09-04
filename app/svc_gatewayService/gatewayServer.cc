#include <chrono>

#include <bite_scaffold/log.h>
#include <bite_scaffold/etcd.h>
#include <bite_scaffold/rpc.h>
#include "gatewayServer.h"

namespace GatewayService {

GatewayServer::GatewayServer(const std::string& host, int port)
    : _host(host)
    , _port(port)
    , _server(nullptr)
    , _isRunning(false) {
    INF("GatewayServer initialized with host: {}, port: {}", _host, _port);
}

GatewayServer::~GatewayServer() {
    if (_isRunning.load()) {
        stop();
    }
}

// 课件原版此析构函数内联在 gatewayServer.h 中且使用 INF 宏，但该头文件未 include log.h，
// 依赖包含顺序才能编译（脆弱设计）；修复为在此处实现
GatewayServerBuilder::~GatewayServerBuilder() {
    INF("GatewayServerBuilder destroyed");
}

// start() 调用
//     │
//     ├── 加锁 (_mutex) ─── 防止并发重复启动
//     │
//     ├── 检查 _isRunning ── 幂等保护，已运行则直接返回 false
//     │
//     ├── 检查 _server ───── 空指针防御，未注入 HTTP Server 则返回 false
//     │
//     ├── detach 新线程 ───→ 执行 _server->listen() (永久阻塞)
//     │                          │
//     │                          ├── 成功：持续接受连接，直到 stop() 被调用
//     │                          └── 失败：_isRunning.store(false) ← 【你修复的关键行】
//     │
//     ├── _isRunning.store(true) ← 标记为运行中
//     │
//     └── 返回 true

bool GatewayServer::start() {
    std::lock_guard<std::mutex> lock(_mutex);

    if (_isRunning.load()) {
        WRN("GatewayServer is already running!");
        return false;
    }

    if (!_server) {
        ERR("GatewayServer http server is null!");
        return false;
    }

    std::thread serverThread([this]() {
        INF("GatewayServer starting on {}:{}", _host, _port);
        if (!_server->listen(_host, _port)) {
            ERR("GatewayServer failed to listen on {}:{}", _host, _port);
            _isRunning.store(false);    // 课件原版缺这行：listen 失败后状态会永远卡在 running，主循环假死；修复加上
        }
    });

    serverThread.detach();

    _isRunning.store(true);
    INF("GatewayServer started successfully!");
    return true;
}

// 在 C++ 的世界里，std::move 配合 unique_ptr，本质上就是一场合法的、编译器保护的 “权力交接”。
// 我们可以顺着你这个“夺权”的思路，把 GatewayServer 和 httplib::Server 的关系看得更透彻：
// 1. GatewayServer 夺了哪些“权”？
// 当 setServer(std::move(server)) 执行的那一瞬间，GatewayServer 从 httplib::Server 手里夺走了三大核心权力：
// 生杀大权（生命周期控制）：httplib::Server 内部持有操作系统的 Socket 端口。夺权后，只有 GatewayServer 销毁时，这个端口才会被释放。Builder 或其他任何代码都无法再“杀掉”这个 HTTP 引擎。
// 指挥权（状态控制）：httplib::Server 自己不知道什么时候该上班、什么时候该下班。夺权后，只有 GatewayServer 的 start() 和 stop() 方法能指挥它启动监听或停止服务。
// 外交权（路由与请求接管）：httplib::Server 原本可以直接对外提供服务，但被夺权后，它变成了“幕后黑手”。所有的 HTTP 请求必须先经过 GatewayServer 的 bindRoutes()（路由表），被拦截并分配给 GatewayServiceImpl（业务逻辑）去处理。
// 2. GatewayServerBuilder 的角色：筹备与“禅让”
// 如果说 GatewayServer 是夺权的“新王”，那 GatewayServerBuilder 就是 “筹备委员会”。
// 筹备阶段：Builder 负责招兵买马（配置 IP、端口）、拉拢盟友（连接 ETCD）、建立军队（new httplib::Server()）。
// 禅让阶段（build）：一旦筹备完毕，Builder 调用 setServer(std::move(...))，把兵权全部交出。
// 退位：build() 函数执行完毕后，Builder 自己的生命周期通常也就结束了（局部变量销毁），它绝不恋栈，绝不干涉“新王”的执政。
// 3. 为什么要“夺权”？（为什么不直接用 httplib::Server？）
// 你可能会问：既然 httplib::Server 已经能处理 HTTP 请求了，为什么还要搞个 GatewayServer 去夺它的权？直接用它不行吗？
// 因为 httplib::Server 只是个“裸”的工具，它不懂业务。
// httplib::Server 不知道什么是“微服务”。
// 它不知道如何去 ETCD 里找后端的 RPC 节点。
// 它不知道怎么做统一的鉴权、限流、日志记录。
// GatewayServer 夺权的目的，是为了给这个“裸”的 HTTP 引擎穿上 “网关的业务外衣”。它把底层的网络通信（httplib）和上层的业务逻辑（GatewayServiceImpl、ETCD 服务发现）强行绑定在一起，对外包装成一个完整的、懂业务的“网关服务器”。
void GatewayServer::stop() {
    std::lock_guard<std::mutex> lock(_mutex);

    if (!_isRunning.load()) {
        WRN("GatewayServer is not running!");
        return;
    }

    if (_server) {
        _server->stop();
    }

    _isRunning.store(false);
    INF("GatewayServer stopped successfully!");
}

bool GatewayServer::isRunning() const {
    return _isRunning.load();
}

void GatewayServer::bindRoutes() {
    if (_serviceImpl && _server) {
        _serviceImpl->bindRoutes(*_server);
        INF("Routes bound to GatewayServer");
    }
}

void GatewayServer::setServer(std::unique_ptr<httplib::Server> server) {
    _server = std::move(server);        // unique_ptr 只能移动：所有权从调用方转移到服务器
}

void GatewayServer::setServiceImpl(std::unique_ptr<GatewayServiceImpl> serviceImpl) {
    _serviceImpl = std::move(serviceImpl);
}

GatewayServerBuilder& GatewayServerBuilder::setHost(const std::string& host) {
    _host = host;
    return *this;
}

GatewayServerBuilder& GatewayServerBuilder::setPort(int port) {
    _port = port;
    return *this;
}

GatewayServerBuilder& GatewayServerBuilder::setEtcdAddr(const std::string& etcdAddr) {
    _etcdAddr = etcdAddr;
    return *this;
}

GatewayServerBuilder& GatewayServerBuilder::addService(const std::string& serviceName) {
    _serviceNames.push_back(serviceName);
    return *this;
}


// | 方法 | 所属组件 | 触发时机 | 核心作用 | 数据流向 |
// | :--- | :--- | :--- | :--- | :--- |
// | `setWatch` | `SvcChannels` | Builder 初始化阶段 | 预分配容器：为指定服务名创建空的节点管理槽位 | 配置 → 内存结构 |
// | `watch` | `SvcWatcher` | 独立后台线程 | 阻塞监听：与 ETCD 建立长连接，实时捕获变更事件 | ETCD → 回调函数 |
// | `addNode` | `SvcChannels` | Online 回调触发时 | 上线注册：将新节点加入可用列表，更新负载均衡池 | 回调 → 路由表 |
// | `delNode` | `SvcChannels` | Offline 回调触发时 | 下线摘除：将故障/下线节点从可用列表移除 | 回调 → 路由表 |


void GatewayServerBuilder::initServiceDiscovery() {
    // 步骤1：为每个服务名称在SvcChannels中创建对应的节点管理集合
    // SvcChannels内部为每个服务维护一个 Channels 对象，用于管理该服务的所有可用节点
    for (const auto& serviceName : _serviceNames) {
        _svcChannels->setWatch(serviceName);
        INF("Set watch for service: {}", serviceName);
    }

    // 步骤2：定义服务上线回调函数
    // 当服务节点在ETCD中注册时，SvcWatcher会触发此回调
    // 回调中调用SvcChannels::addNode将新节点添加到对应服务的节点集合中
    auto onlineCallback = [this](const std::string& serviceName, const std::string& serviceAddr) {
        INF("Service online: {} at {}", serviceName, serviceAddr);
        _svcChannels->addNode(serviceName, serviceAddr);
    };

    // 步骤3：定义服务下线回调函数
    // 当服务节点从ETCD中注销时，SvcWatcher会触发此回调
    // 回调中调用SvcChannels::delNode将下线节点从对应服务的节点集合中移除
    auto offlineCallback = [this](const std::string& serviceName, const std::string& serviceAddr) {
        INF("Service offline: {} at {}", serviceName, serviceAddr);
        _svcChannels->delNode(serviceName, serviceAddr);
    };

    // 步骤4：创建SvcWatcher实例
    // SvcWatcher负责连接ETCD注册中心，监控服务节点的上下线事件
    _serviceWatcher = std::make_shared<bitesvc::SvcWatcher>(_etcdAddr, onlineCallback, offlineCallback);

    // 步骤5：启动独立的监控线程
    // watch()方法会阻塞当前线程，因此需要在新线程中运行
    // 新线程会持续监控ETCD，服务节点变化时触发回调函数
    std::thread watcherThread([this]() {
        _serviceWatcher->watch();
    });
    watcherThread.detach();

    INF("Service discovery watcher started");
}

std::shared_ptr<GatewayServer> GatewayServerBuilder::build() {
    // 0. 构建SvcChannels实例
    _svcChannels = std::make_shared<biterpc::SvcChannels>();

    // 步骤1：初始化服务发现组件
    // 创建SvcChannels管理所有服务的节点信道，创建SvcWatcher监听ETCD服务变化
    initServiceDiscovery();

    // 步骤2：创建网关服务实现实例
    // 将SvcChannels引用传递给GatewayServiceImpl，供其获取后端服务的Channel
    auto serviceImpl = std::make_unique<GatewayServiceImpl>(_svcChannels, _serviceWatcher);

    // 步骤3：创建网关服务器实例
    auto server = std::make_shared<GatewayServer>(_host, _port);

    // 步骤4：创建并配置HTTP服务器
    // 设置读写超时时间为5分钟，支持长连接和流式响应（AI 流式回答可能持续几分钟，默认超时会掐断连接）
    auto httpServer = std::make_unique<httplib::Server>();
    httpServer->set_read_timeout(std::chrono::minutes(5));
    httpServer->set_write_timeout(std::chrono::minutes(5));

    // 步骤5：注入HTTP服务器和服务实现到网关服务器
    server->setServer(std::move(httpServer));
    server->setServiceImpl(std::move(serviceImpl));

    // 步骤6：绑定路由
    // 将GatewayServiceImpl中的30个HTTP接口绑定到HTTP服务器
    server->bindRoutes();

    // 步骤7：启动服务器
    // 服务器会在独立线程中运行，监听指定地址和端口
    if (!server->start()) {
        ERR("Failed to start GatewayServer");
        return nullptr;
    }

    // 步骤8：保存服务器实例并返回
    _server = server;
    INF("GatewayServer built and started successfully");
    return server;
}

} // end GatewayService

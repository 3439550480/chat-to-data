#pragma once
#include <string>
#include <vector>
#include <memory>
#include <brpc/server.h>
#include "excelParserServiceImpl.h"

namespace bitesvc {
class SvcProvider;
}

namespace excelParserService {

// 注册中心配置结构
struct RegisterCenterConfig {
    std::string _etcdAddr;       // etcd注册中心地址
    std::string _serviceName;    // 服务名称
    std::string _serviceAddr;    // 服务地址
};

// FastDFS配置结构（课件问题㉙修复：int 成员补默认值——课件 main.cc 从未调用
// setFastDFSConfig，Builder 里的 int 是垃圾值）
struct FastDFSConfig {
    std::vector<std::string> _trackers = {};   // tracker服务器地址列表
    int _connectTimeout = 30;                  // 连接超时（秒，与脚手架 fdfs_settings 默认一致）
    int _networkTimeout = 30;                  // 网络超时（秒）
};

// RPC服务器类
class ExcelParserServer {
public:
    explicit ExcelParserServer(std::shared_ptr<ExcelParserServiceImpl> serviceImpl,
                               std::shared_ptr<brpc::Server> server,
                               std::shared_ptr<bitesvc::SvcProvider> serviceProvider);
    ~ExcelParserServer();
    // 启动RPC服务器（阻塞直至退出信号）
    void start();
private:
    std::shared_ptr<brpc::Server> _server;                      // brpc服务器实例
    std::shared_ptr<ExcelParserServiceImpl> _serviceImpl;       // 接口层实例
    std::shared_ptr<bitesvc::SvcProvider> _serviceProvider;     // 服务注册层实例
};

// RPC服务器构建器类
class ExcelParserServerBuilder {
public:
    ~ExcelParserServerBuilder();
    // 设置RPC服务器端口
    ExcelParserServerBuilder& setPort(int port);
    // 设置注册中心配置
    ExcelParserServerBuilder& setRegisterCenterConfig(const RegisterCenterConfig& config);
    // 设置FastDFS配置（trackers 为空时使用占位配置启动，真实下载等文件子服务章节联调）
    ExcelParserServerBuilder& setFastDFSConfig(const FastDFSConfig& fastDFSConfig);
    // 构建RPC服务器实例
    std::shared_ptr<ExcelParserServer> build();
private:
    int _port;                                    // RPC服务器端口
    RegisterCenterConfig _registerCenterConfig;   // 注册中心配置
    FastDFSConfig _fastDFSConfig;                 // FastDFS配置
};

} // namespace excelParserService
